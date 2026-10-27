// Lines up the lock on door and chest models outside the game, in NifSkope (see README.md).
//
//   lockplacer place <list> <out dir> <data dir>[;<data dir>...] [VRLockpicking_Locks.ini]
//     For every model in the list the plugin writes (VRLockpicking_LockableModels.txt), copies
//     the model from the extracted game data into <out dir>\meshes\..., with a marker where the
//     lock sits: the place saved in the INI, else the plugin's guess from the model's size. A
//     copy that is already there keeps its marker (you may have moved it).
//   lockplacer read <list> <out dir> <VRLockpicking_Locks.ini>
//     Reads where the markers are now and saves them to the INI (vObjectKeyhole / vObjectInto /
//     vObjectUp), the same file and keys the in-game markup tool writes.
//
// The marker is a node named VRLP_Lock under the model's root: its origin is the keyhole, +Y
// points into the door or chest, +Z is up. Move and turn it with NifSkope's Transform > Edit.

#include <NifFile.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "Config.h"
#include "LockProfile.h"

namespace fs = std::filesystem;
using nifly::Color4;
using nifly::Triangle;
using nifly::Vector2;
using nifly::Vector3;

namespace
{
	constexpr const char* kMarker = "VRLP_Lock";

	std::string ReadText(const fs::path& a_path)
	{
		std::ifstream     in(a_path, std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}

	std::vector<std::string> Split(const std::string& a_s, char a_sep)
	{
		std::vector<std::string> out;
		std::size_t              p = 0;
		while (p <= a_s.size()) {
			const std::size_t q = a_s.find(a_sep, p);
			const std::string part = a_s.substr(p, q == std::string::npos ? std::string::npos : q - p);
			if (!part.empty()) {
				out.push_back(part);
			}
			if (q == std::string::npos) {
				break;
			}
			p = q + 1;
		}
		return out;
	}

	std::string Lower(std::string a_s)
	{
		for (auto& c : a_s) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return a_s;
	}

	// a_base / a_relative (backslashes), whatever the case of each part (on Windows the file
	// system does that itself)
	fs::path Find(const fs::path& a_base, const std::string& a_relative)
	{
		fs::path p = a_base;
		for (const auto& part : Split(a_relative, '\\')) {
			fs::path next = p / part;
			if (!fs::exists(next) && fs::is_directory(p)) {
				for (const auto& e : fs::directory_iterator(p)) {
					if (Lower(e.path().filename().string()) == Lower(part)) {
						next = e.path();
						break;
					}
				}
			}
			p = next;
		}
		return p;
	}

	fs::path OutPath(const fs::path& a_out, const std::string& a_model)
	{
		fs::path p = a_out / "meshes";
		for (const auto& part : Split(a_model, '\\')) {
			p /= part;
		}
		return p;
	}

	nifly::MatTransform ToNif(const vrlp::ObjectAnchor& a_a)
	{
		nifly::MatTransform t;
		t.translation = Vector3(a_a.keyhole.x, a_a.keyhole.y, a_a.keyhole.z);
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				t.rotation[i][j] = a_a.rot.m[i][j];
			}
		}
		return t;
	}

	vrlp::ObjectAnchor FromNif(const nifly::MatTransform& a_t)
	{
		vrlp::Pose lock;
		lock.pos = { a_t.translation.x, a_t.translation.y, a_t.translation.z };
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				lock.rot.m[i][j] = a_t.rotation[i][j];
			}
		}
		return vrlp::ObjectAnchorFromPose(lock, vrlp::Pose{}, 1.0f);  // also cleans up the rotation
	}

	struct Mesh
	{
		std::vector<Vector3>  v;
		std::vector<Triangle> t;
		std::vector<Vector2>  uv;
		std::vector<Vector3>  n;
		std::vector<Color4>   c;

		void Box(Vector3 a_lo, Vector3 a_hi, Color4 a_colour)
		{
			const Vector3& a = a_lo;
			const Vector3& b = a_hi;
			const Vector3  faces[6][4] = {
                { Vector3(b.x, a.y, a.z), Vector3(b.x, b.y, a.z), Vector3(b.x, b.y, b.z), Vector3(b.x, a.y, b.z) },
                { Vector3(a.x, b.y, a.z), Vector3(a.x, a.y, a.z), Vector3(a.x, a.y, b.z), Vector3(a.x, b.y, b.z) },
                { Vector3(b.x, b.y, a.z), Vector3(a.x, b.y, a.z), Vector3(a.x, b.y, b.z), Vector3(b.x, b.y, b.z) },
                { Vector3(a.x, a.y, a.z), Vector3(b.x, a.y, a.z), Vector3(b.x, a.y, b.z), Vector3(a.x, a.y, b.z) },
                { Vector3(a.x, a.y, b.z), Vector3(b.x, a.y, b.z), Vector3(b.x, b.y, b.z), Vector3(a.x, b.y, b.z) },
                { Vector3(a.x, b.y, a.z), Vector3(b.x, b.y, a.z), Vector3(b.x, a.y, a.z), Vector3(a.x, a.y, a.z) },
			};
			const Vector3 normals[6] = { Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, -1, 0), Vector3(0, 0, 1), Vector3(0, 0, -1) };
			for (int f = 0; f < 6; ++f) {
				const auto base = static_cast<std::uint16_t>(v.size());
				for (int k = 0; k < 4; ++k) {
					v.push_back(faces[f][k]);
					n.push_back(normals[f]);
					uv.emplace_back(k == 1 || k == 2 ? 1.0f : 0.0f, k >= 2 ? 0.0f : 1.0f);
					c.push_back(a_colour);
				}
				t.emplace_back(base, base + 1, base + 2);
				t.emplace_back(base, base + 2, base + 3);
			}
		}
	};

	// The marker in the lock frame (x right, y into the object, z up, keyhole at the origin): the
	// outline of the lock's face as the plugin sizes it, the keyhole, an arrow into the object and
	// a tick on top.
	Mesh MarkerMesh()
	{
		const vrlp::Config cfg;
		const float        r = cfg.lockMeshRadius * cfg.visualScale;
		const float        w = 0.35f;
		const Color4       white(0.95f, 0.95f, 0.95f, 1.0f), red(0.95f, 0.15f, 0.15f, 1.0f), green(0.2f, 0.9f, 0.3f, 1.0f),
			blue(0.25f, 0.5f, 1.0f, 1.0f);
		Mesh m;
		m.Box(Vector3(-r, -w, r - w), Vector3(r, w, r + w), white);    // top
		m.Box(Vector3(-r, -w, -r - w), Vector3(r, w, -r + w), white);  // bottom
		m.Box(Vector3(-r - w, -w, -r), Vector3(-r + w, w, r), white);  // left
		m.Box(Vector3(r - w, -w, -r), Vector3(r + w, w, r), white);    // right
		m.Box(Vector3(-0.35f, -0.6f, -1.7f), Vector3(0.35f, 0.6f, 1.7f), red);  // the keyhole
		m.Box(Vector3(-0.2f, 0.0f, -0.2f), Vector3(0.2f, 12.0f, 0.2f), green);  // into the object
		m.Box(Vector3(-0.3f, -w, r + w), Vector3(0.3f, w, r + 3.0f), blue);     // up
		return m;
	}

	bool AddMarker(nifly::NifFile& a_nif, const vrlp::ObjectAnchor& a_anchor)
	{
		auto* root = a_nif.GetRootNode();
		if (!root) {
			return false;
		}
		auto* node = a_nif.AddNode(kMarker, ToNif(a_anchor), root);
		Mesh  m = MarkerMesh();
		auto* shape = a_nif.CreateShapeFromData("VRLP_LockMarker", &m.v, &m.t, &m.uv, &m.n);
		if (!node || !shape) {
			return false;
		}
		a_nif.SetColorsForShape(shape, m.c);
		if (auto* shader = dynamic_cast<nifly::BSLightingShaderProperty*>(a_nif.GetShader(shape))) {
			shader->shaderFlags1 |= nifly::SLSF1_OWN_EMIT;
			shader->shaderFlags1 &= ~nifly::SLSF1_CAST_SHADOWS;
			shader->shaderFlags2 |= nifly::SLSF2_VERTEX_COLORS | nifly::SLSF2_DOUBLE_SIDED;
			shader->emissiveColor = Vector3(0.6f, 0.6f, 0.6f);
		}
		a_nif.SetParentNode(shape, node);
		return true;
	}

	int Place(const std::vector<vrlp::LockableModel>& a_list, const fs::path& a_out, const std::vector<std::string>& a_data, const fs::path& a_ini)
	{
		const auto saved = a_ini.empty() ? std::map<std::string, vrlp::ObjectAnchor>{} : vrlp::ParseObjectAnchors(ReadText(a_ini));
		int        placed = 0, kept = 0, missing = 0, failed = 0;
		for (const auto& m : a_list) {
			const std::string key = vrlp::NormalizeModelPath(m.model);
			const fs::path    out = OutPath(a_out, key);
			if (fs::exists(out)) {
				nifly::NifFile existing;
				if (existing.Load(out) == 0 && existing.FindBlockByName<nifly::NiNode>(kMarker)) {
					++kept;
					continue;
				}
			}
			fs::path source;
			for (const auto& dir : a_data) {
				const fs::path p = Find(fs::path(dir), "meshes\\" + key);
				if (fs::exists(p)) {
					source = p;
					break;
				}
			}
			if (source.empty()) {
				std::printf("missing  %s (not in the data folders)\n", key.c_str());
				++missing;
				continue;
			}
			nifly::NifFile nif;
			if (nif.Load(source) != 0) {
				std::printf("failed   %s (could not read it)\n", key.c_str());
				++failed;
				continue;
			}
			const char*        from = "saved";
			vrlp::ObjectAnchor anchor;
			if (auto it = saved.find(key); it != saved.end()) {
				anchor = it->second;
			} else if (auto def = vrlp::DefaultObjectAnchors().find(key); def != vrlp::DefaultObjectAnchors().end()) {
				anchor = def->second;
				from = "built in";
			} else {
				anchor = vrlp::GuessObjectAnchor(m.boundMin, m.boundMax, m.door);
				from = "guessed";
			}
			fs::create_directories(out.parent_path());
			if (!AddMarker(nif, anchor) || nif.Save(out) != 0) {
				std::printf("failed   %s (could not write it)\n", key.c_str());
				++failed;
				continue;
			}
			std::printf("placed   %s (%s, %s, locked %d times)\n", key.c_str(), m.door ? "door" : "container", from, m.locked);
			++placed;
		}
		// the marker alone, to copy into other models
		{
			nifly::NifFile nif;
			nif.Create(nifly::NiVersion::getSSE());
			vrlp::ObjectAnchor origin;
			origin.valid = true;
			AddMarker(nif, origin);
			fs::create_directories(a_out);
			nif.Save(a_out / "VRLP_LockMarker.nif");
		}
		std::printf("\n%d placed, %d kept as they were, %d missing, %d failed. Open them in NifSkope from %s\n", placed, kept, missing, failed,
			(a_out / "meshes").string().c_str());
		return failed > 0 ? 1 : 0;
	}

	int Read(const std::vector<vrlp::LockableModel>& a_list, const fs::path& a_out, const fs::path& a_ini)
	{
		std::string text = fs::exists(a_ini) ? ReadText(a_ini) : std::string("; VR Lockpicking model markup\n");
		int         saved = 0, without = 0;
		for (const auto& m : a_list) {
			const std::string key = vrlp::NormalizeModelPath(m.model);
			const fs::path    path = OutPath(a_out, key);
			nifly::NifFile    nif;
			if (!fs::exists(path) || nif.Load(path) != 0) {
				continue;
			}
			auto* node = nif.FindBlockByName<nifly::NiNode>(kMarker);
			if (!node) {
				std::printf("no marker %s\n", key.c_str());
				++without;
				continue;
			}
			if (nif.GetParentNode(node) != nif.GetRootNode()) {
				std::printf("note     %s: the marker is no longer right under the root; its place is read as if it were\n", key.c_str());
			}
			const vrlp::ObjectAnchor a = FromNif(node->GetTransformToParent());
			text = vrlp::WriteObjectAnchor(text, key, a);
			std::printf("saved    %s: %s\n", key.c_str(), vrlp::ObjectAnchorText(a).c_str());
			++saved;
		}
		std::ofstream out(a_ini, std::ios::binary | std::ios::trunc);
		out << text;
		if (!out) {
			std::printf("could not write %s\n", a_ini.string().c_str());
			return 1;
		}
		std::printf("\n%d saved to %s (%d without a marker)\n", saved, a_ini.string().c_str(), without);
		return 0;
	}

	int Usage()
	{
		std::fprintf(stderr,
			"usage:\n"
			"  lockplacer place <VRLockpicking_LockableModels.txt> <out dir> <data dir>[;<data dir>...] [VRLockpicking_Locks.ini]\n"
			"  lockplacer read  <VRLockpicking_LockableModels.txt> <out dir> <VRLockpicking_Locks.ini>\n");
		return 2;
	}
}

int main(int argc, char** argv)
{
	if (argc < 5) {
		return Usage();
	}
	const std::string command = argv[1];
	const auto        list = vrlp::ParseLockableModels(ReadText(argv[2]));
	if (list.empty()) {
		std::fprintf(stderr, "no models in %s\n", argv[2]);
		return 1;
	}
	if (command == "place") {
		return Place(list, argv[3], Split(argv[4], ';'), argc > 5 ? fs::path(argv[5]) : fs::path());
	}
	if (command == "read") {
		return Read(list, argv[3], argv[4]);
	}
	return Usage();
}
