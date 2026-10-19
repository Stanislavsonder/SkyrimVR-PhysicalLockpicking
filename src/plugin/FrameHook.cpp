#include "FrameHook.h"

namespace vrlp::FrameHook
{
	namespace
	{
		// Skyrim VR 1.4.15 has a single build, so a fixed offset is safe: a 5-byte call in
		// Main::Draw shortly after the wand nodes are updated (HIGGS hooks the same spot).
		constexpr std::uintptr_t kSiteOffset = 0x13233AA;

		Callback                                  g_callback = nullptr;
		std::atomic<std::int64_t>                 g_lastFire{ 0 };

		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		void OnPostWandUpdate()
		{
			g_lastFire.store(NowMs());
			if (g_callback) {
				g_callback();
			}
		}

		struct Writer
		{
			std::uint8_t* p;
			void          bytes(std::initializer_list<std::uint8_t> a_b)
			{
				for (auto b : a_b) {
					*p++ = b;
				}
			}
			void qword(std::uint64_t a_v)
			{
				std::memcpy(p, &a_v, 8);
				p += 8;
			}
		};

		// Calls OnPostWandUpdate keeping rax / xmm0 (the hooked call's result) and the stack
		// 16-byte aligned, then jumps to a_back.
		void WriteTail(Writer& w, std::uintptr_t a_back)
		{
			w.bytes({ 0x50 });                                // push rax
			w.bytes({ 0x48, 0x83, 0xEC, 0x38 });              // sub rsp, 38h
			w.bytes({ 0xF2, 0x0F, 0x11, 0x44, 0x24, 0x20 });  // movsd [rsp+20h], xmm0
			w.bytes({ 0x48, 0xB8 });                          // mov rax, OnPostWandUpdate
			w.qword(reinterpret_cast<std::uint64_t>(&OnPostWandUpdate));
			w.bytes({ 0xFF, 0xD0 });                          // call rax
			w.bytes({ 0xF2, 0x0F, 0x10, 0x44, 0x24, 0x20 });  // movsd xmm0, [rsp+20h]
			w.bytes({ 0x48, 0x83, 0xC4, 0x38 });              // add rsp, 38h
			w.bytes({ 0x58 });                                // pop rax
			w.bytes({ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 });  // jmp [rip]
			w.qword(a_back);
		}
	}

	bool Install(Callback a_callback)
	{
		g_callback = a_callback;
		const std::uintptr_t site = REL::Offset(kSiteOffset).address();
		const std::uintptr_t back = site + 5;
		const auto*          code = reinterpret_cast<const std::uint8_t*>(site);
		std::int32_t         rel = 0;
		std::memcpy(&rel, code + 1, 4);
		const std::uintptr_t target = back + static_cast<std::intptr_t>(rel);

		auto& trampoline = SKSE::GetTrampoline();
		if (code[0] == 0xE8) {
			// the game's own call: call it from our code, then ours
			auto* mem = static_cast<std::uint8_t*>(trampoline.allocate(96));
			Writer w{ mem };
			w.bytes({ 0x48, 0xB8 });  // mov rax, original
			w.qword(target);
			w.bytes({ 0xFF, 0xD0 });  // call rax
			WriteTail(w, back);
			trampoline.write_branch<5>(site, reinterpret_cast<std::uintptr_t>(mem), true);
			logger::info("Frame hook: installed after the wand update");
			return true;
		}
		if (code[0] == 0xE9) {
			// Another mod (HIGGS) got there first: its code ends by jumping back to the site + 5
			// through a stored address. Point that address at our tail instead, which then
			// jumps back itself.
			auto*       other = reinterpret_cast<std::uint8_t*>(target);
			std::size_t found = 0, at = 0;
			for (std::size_t i = 0; i + 8 <= 256; ++i) {
				std::uint64_t v = 0;
				std::memcpy(&v, other + i, 8);
				if (v == back) {
					++found;
					at = i;
				}
			}
			if (found != 1) {
				logger::info("Frame hook: the site is taken by another mod and its return could not be found ({} candidates); using the controller callback", found);
				return false;
			}
			auto*  mem = static_cast<std::uint8_t*>(trampoline.allocate(64));
			Writer w{ mem };
			WriteTail(w, back);
			const std::uint64_t tail = reinterpret_cast<std::uint64_t>(mem);
			REL::safe_write(reinterpret_cast<std::uintptr_t>(other + at), &tail, sizeof(tail));
			logger::info("Frame hook: chained after another mod's hook at the wand update");
			return true;
		}
		logger::info("Frame hook: unexpected code at the site ({:02X}); using the controller callback", code[0]);
		return false;
	}

	bool Alive()
	{
		return NowMs() - g_lastFire.load() < 100;
	}
}
