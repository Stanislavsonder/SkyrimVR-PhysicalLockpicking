// Lines up the lock on door and chest models outside the game, in NifSkope (see README.md).
//
//   lockplacer place <list> <out dir> <data>[;<data>...] [VRLockpicking_Locks.ini]
//     For every model in the list the plugin writes (VRLockpicking_LockableModels.txt), copies
//     the model from the game's data into <out dir>\meshes\..., with a marker where the
//     lock sits: the place saved in the INI, else the plugin's guess from the model's size. A
//     copy that is already there is made again, its marker where it was (you may have moved it).
//     The marker shows the lock the mod draws, when the data has it.
//     Each <data> is a folder (the game's Data, a mod's folder, extracted meshes) or one .bsa. A
//     folder's loose meshes come first, then its .bsa archives, the game's later ones first, as
//     the game reads them.
//   lockplacer read <list> <out dir> <VRLockpicking_Locks.ini>
//     Reads where the markers are now and saves them to the INI (vObjectKeyhole / vObjectInto /
//     vObjectUp), the same file and keys the in-game markup tool writes.
//
// The marker is a node named VRLP_Lock under the model's root: its origin is the keyhole, +Y
// points into the door or chest, +Z is up. Move and turn it with NifSkope's Transform > Edit.
// A second VRLP_Lock (a copy of the first) marks a door's other side (vObjectKeyhole2 ...).

#include <NifFile.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "Bsa.h"
#include "Config.h"
#include "LockProfile.h"
#include "MeshFit.h"

namespace fs = std::filesystem;
using nifly::Color4;
using nifly::Triangle;
using nifly::Vector2;
using nifly::Vector3;

namespace
{
	constexpr const char* kMarker = "VRLP_Lock";
	constexpr const char* kLockBody = "VRLP_LockBody";

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

	// Where the models come from: folders of loose files and the archives in them, by priority.
	class Sources
	{
	public:
		explicit Sources(const std::vector<std::string>& a_data)
		{
			for (const auto& d : a_data) {
				const fs::path path(d);
				if (fs::is_directory(path)) {
					_items.push_back({ path, nullptr });
					std::vector<fs::path> archives;
					for (const auto& e : fs::directory_iterator(path)) {
						if (e.is_regular_file() && Lower(e.path().extension().string()) == ".bsa") {
							archives.push_back(e.path());
						}
					}
					std::sort(archives.begin(), archives.end(), [](const fs::path& a_l, const fs::path& a_r) {
						const int l = Rank(a_l), r = Rank(a_r);
						return l != r ? l > r : Lower(a_l.filename().string()) > Lower(a_r.filename().string());
					});
					for (const auto& a : archives) {
						AddArchive(a);
					}
				} else if (Lower(path.extension().string()) == ".bsa") {
					AddArchive(path);
				} else {
					std::printf("note     %s is neither a folder nor a .bsa\n", d.c_str());
				}
			}
		}

		// The model's file as it is, and where it was found; empty when nowhere.
		std::optional<std::string> Get(const std::string& a_key, std::string& a_from)
		{
			const std::string inArchive = "meshes\\" + a_key;
			for (auto& item : _items) {
				if (!item.archive) {
					const fs::path p = Find(item.path, inArchive);
					if (fs::exists(p)) {
						a_from = p.string();
						return ReadText(p);
					}
				} else if (item.archive->Contains(inArchive)) {
					a_from = item.path.filename().string();
					if (auto data = item.archive->Read(inArchive)) {
						return std::string(data->begin(), data->end());
					}
					std::printf("note     %s: could not unpack it from %s\n", a_key.c_str(), a_from.c_str());
				}
			}
			return std::nullopt;
		}

	private:
		struct Item
		{
			fs::path                          path;
			std::unique_ptr<vrlp::BsaArchive> archive;
		};

		// The game loads its own archives first and in this order; the rest (mods) after them.
		static int Rank(const fs::path& a_path)
		{
			const std::string n = Lower(a_path.filename().string());
			const char*       order[] = { "skyrim - ", "update.bsa", "dawnguard.bsa", "hearthfires.bsa", "dragonborn.bsa", "skyrim_vr" };
			for (int i = 0; i < 6; ++i) {
				if (n.starts_with(order[i])) {
					return i;
				}
			}
			return 6;
		}

		void AddArchive(const fs::path& a_path)
		{
			auto archive = std::make_unique<vrlp::BsaArchive>();
			if (!archive->Open(a_path.string())) {
				std::printf("note     %s: not an archive this can read\n", a_path.string().c_str());
				return;
			}
			_items.push_back({ a_path, std::move(archive) });
		}

		std::vector<Item> _items;
	};

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

	// Where a marker is, and whether its lock's body was deleted or hidden (only the keyhole shows).
	vrlp::ObjectAnchor MarkerAnchor(nifly::NifFile& a_nif, nifly::NiNode* a_marker)
	{
		vrlp::ObjectAnchor a = FromNif(a_marker->GetTransformToParent());
		bool               lockShown = false;
		nifly::NiNode*     body = nullptr;
		for (auto* child : a_nif.GetChildren<nifly::NiAVObject>(a_marker)) {
			const std::string name = child->name.get();
			if (name == kLockBody) {
				body = dynamic_cast<nifly::NiNode*>(child);
			}
			lockShown = lockShown || (name.starts_with("VRLP_Lock") && name != "VRLP_LockMarker");
		}
		a.keyholeOnly = lockShown && (!body || (body->flags & 1) || a_nif.GetChildren<nifly::NiShape>(body).empty());
		return a;
	}

	// The VRLP_Lock nodes, in the file's order: the marked side, then a door's other side.
	std::vector<nifly::NiNode*> Markers(nifly::NifFile& a_nif)
	{
		std::vector<nifly::NiNode*> out;
		auto&                       hdr = a_nif.GetHeader();
		for (uint32_t i = 0; i < hdr.GetNumBlocks(); ++i) {
			if (auto* node = hdr.GetBlock<nifly::NiNode>(i); node && node->name.get() == kMarker) {
				out.push_back(node);
			}
		}
		return out;
	}

	// Where the markers are: the first one, and the second as the door's other side.
	std::optional<vrlp::ObjectAnchor> MarkersAnchor(nifly::NifFile& a_nif, const std::string& a_key)
	{
		const auto markers = Markers(a_nif);
		if (markers.empty()) {
			return std::nullopt;
		}
		for (auto* node : markers) {
			if (a_nif.GetParentNode(node) != a_nif.GetRootNode()) {
				std::printf("note     %s: a marker is no longer right under the root; its place is read as if it were\n", a_key.c_str());
			}
		}
		if (markers.size() > 2) {
			std::printf("note     %s: %zu markers; the first two are used (one for each side of a door)\n", a_key.c_str(), markers.size());
		}
		vrlp::ObjectAnchor a = MarkerAnchor(a_nif, markers[0]);
		if (markers.size() >= 2) {
			const vrlp::ObjectAnchor b = MarkerAnchor(a_nif, markers[1]);
			a.back = vrlp::ObjectAnchor::Side{ b.keyhole, b.rot, b.keyholeOnly };
		}
		return a;
	}

	// Collision isn't needed to line up a lock, and NifSkope can fail to save some of it
	// (bhkCompressedMeshShapeData): the preview copies have none.
	int StripCollision(nifly::NifFile& a_nif)
	{
		auto& hdr = a_nif.GetHeader();
		int   stripped = 0;
		for (uint32_t i = 0; i < hdr.GetNumBlocks(); ++i) {
			if (auto* obj = hdr.GetBlock<nifly::NiAVObject>(i); obj && !obj->collisionRef.IsEmpty()) {
				obj->collisionRef.Clear();
				++stripped;
			}
		}
		if (stripped > 0) {
			uint32_t deleted = 0;
			hdr.DeleteUnreferencedBlocks<nifly::NiObject>(a_nif.GetBlockID(a_nif.GetRootNode()), &deleted);
		}
		return stripped;
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
	// outline of the lock's face as the plugin sizes it (when the lock itself isn't shown), the
	// keyhole, an arrow into the object and a tick on top.
	Mesh MarkerMesh(bool a_frame)
	{
		const vrlp::Config cfg;
		const float        r = cfg.lockMeshRadius * cfg.visualScale;
		const float        w = 0.35f;
		const Color4       white(0.95f, 0.95f, 0.95f, 1.0f), red(0.95f, 0.15f, 0.15f, 1.0f), green(0.2f, 0.9f, 0.3f, 1.0f),
			blue(0.25f, 0.5f, 1.0f, 1.0f);
		Mesh m;
		if (a_frame) {
			m.Box(Vector3(-r, -w, r - w), Vector3(r, w, r + w), white);    // top
			m.Box(Vector3(-r, -w, -r - w), Vector3(r, w, -r + w), white);  // bottom
			m.Box(Vector3(-r - w, -w, -r), Vector3(-r + w, w, r), white);  // left
			m.Box(Vector3(r - w, -w, -r), Vector3(r + w, w, r), white);    // right
		}
		m.Box(Vector3(-0.35f, -0.6f, -1.7f), Vector3(0.35f, 0.6f, 1.7f), red);  // the keyhole
		m.Box(Vector3(-0.2f, 0.0f, -0.2f), Vector3(0.2f, 12.0f, 0.2f), green);  // into the object
		m.Box(Vector3(-0.3f, -w, r + w), Vector3(0.3f, w, r + 3.0f), blue);     // up
		return m;
	}

	// The lock the mod draws (the vanilla lockpicking menu's), placed in the lock frame the way the
	// plugin places it (MeshFit from its parts' bounds), without its screwdriver.
	struct LockModel
	{
		struct Shape
		{
			nifly::NiShape*     shape;
			nifly::MatTransform place;  // in the lock frame
			bool                body;   // not the turning part (hidden when only the keyhole shows)
		};
		nifly::NifFile     nif;
		std::vector<Shape> shapes;
		bool               ok{ false };
	};

	bool NameHas(const nifly::NiObjectNET* a_obj, const char* a_needle) { return Lower(a_obj->name.get()).find(a_needle) != std::string::npos; }

	void LoadLock(LockModel& a_lock, Sources& a_sources)
	{
		const vrlp::Config cfg;
		const std::string  key = vrlp::NormalizeModelPath(cfg.lockMesh);
		std::string        from;
		const auto         file = a_sources.Get(key, from);
		std::istringstream in(file ? *file : std::string());
		if (!file || a_lock.nif.Load(in) != 0) {
			std::printf("note     the lock model (%s) isn't in the data: markers show only a frame\n", key.c_str());
			return;
		}
		struct Found
		{
			nifly::NiShape*     shape;
			nifly::MatTransform model;  // shape -> model space
			bool                shiv;
			bool                cylinder;
		};
		std::vector<Found>           found;
		std::vector<vrlp::MeshPart>  parts;
		vrlp::Vec3                   shivSum;
		int                          shivCount = 0;
		std::function<void(nifly::NiNode*, const nifly::MatTransform&, bool, bool, int)> walk =
			[&](nifly::NiNode* a_node, const nifly::MatTransform& a_xf, bool a_shiv, bool a_cyl, int a_depth) {
				for (auto* child : a_lock.nif.GetChildren<nifly::NiAVObject>(a_node)) {
					const nifly::MatTransform xf = a_xf.ComposeTransforms(child->GetTransformToParent());
					const bool                shiv = a_shiv || NameHas(child, "shiv") || NameHas(child, "screwdriver");
					if (auto* node = dynamic_cast<nifly::NiNode*>(child)) {
						bool cyl = a_cyl;
						for (const char* n : { "tumble", "cylinder", "keyhole", "inner", "plug" }) {
							cyl = cyl || NameHas(node, n);
						}
						if (a_depth < 32) {
							walk(node, xf, shiv, cyl, a_depth + 1);
						}
					} else if (auto* shape = dynamic_cast<nifly::NiShape*>(child)) {
						std::vector<Vector3> verts;
						if (!a_lock.nif.GetVertsForShape(shape, verts) || verts.empty()) {
							continue;
						}
						Vector3 lo = verts.front(), hi = lo;
						for (const auto& v : verts) {
							lo = Vector3(std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z));
							hi = Vector3(std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z));
						}
						vrlp::MeshPart part;
						part.cylinder = a_cyl;
						vrlp::Vec3 sum;
						for (int i = 0; i < 8; ++i) {
							const Vector3 q = xf.ApplyTransform(Vector3((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z));
							part.corners.push_back({ q.x, q.y, q.z });
							sum += part.corners.back();
						}
						part.centre = sum / 8.0f;
						for (const auto& q : part.corners) {
							part.radius = std::max(part.radius, vrlp::Distance(q, part.centre));
						}
						if (shiv) {
							shivSum += part.centre;
							++shivCount;
						} else if (part.radius > 0.0f) {
							parts.push_back(part);
						}
						found.push_back({ shape, xf, shiv, a_cyl });
					}
				}
			};
		walk(nullptr, nifly::MatTransform(), false, false, 0);
		const vrlp::Vec3    front = shivCount > 0 ? shivSum / static_cast<float>(shivCount) : vrlp::Vec3{ 0.0f, -1000.0f, 0.0f };
		const vrlp::LockFit fit = vrlp::FitLockParts(parts, front, cfg.lockMeshRadius * cfg.visualScale);
		if (!fit.place.valid) {
			std::printf("note     could not fit the lock model (%s): markers show only a frame\n", key.c_str());
			return;
		}
		nifly::MatTransform place;
		place.translation = Vector3(fit.place.offset.x, fit.place.offset.y, fit.place.offset.z);
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				place.rotation[i][j] = fit.place.rot.m[i][j];
			}
		}
		place.scale = fit.place.scale;
		for (const auto& f : found) {
			if (!f.shiv) {
				a_lock.shapes.push_back({ f.shape, place.ComposeTransforms(f.model), !f.cylinder });
			}
		}
		a_lock.ok = !a_lock.shapes.empty();
		std::printf("lock     %s from %s: %zu part(s) shown on the markers\n", key.c_str(), from.c_str(), a_lock.shapes.size());
	}

	bool AddMarker(nifly::NifFile& a_nif, const vrlp::ObjectAnchor& a_anchor, LockModel* a_lock = nullptr);

	// The marked side, and a door's other side when it was marked too.
	bool AddMarkers(nifly::NifFile& a_nif, const vrlp::ObjectAnchor& a_anchor, LockModel* a_lock)
	{
		vrlp::ObjectAnchor front = a_anchor;
		front.back.reset();
		if (!AddMarker(a_nif, front, a_lock)) {
			return false;
		}
		if (!a_anchor.back) {
			return true;
		}
		vrlp::ObjectAnchor back;
		back.keyhole = a_anchor.back->keyhole;
		back.rot = a_anchor.back->rot;
		back.keyholeOnly = a_anchor.back->keyholeOnly;
		back.valid = true;
		return AddMarker(a_nif, back, a_lock);
	}

	bool AddMarker(nifly::NifFile& a_nif, const vrlp::ObjectAnchor& a_anchor, LockModel* a_lock)
	{
		auto* root = a_nif.GetRootNode();
		if (!root) {
			return false;
		}
		auto* node = a_nif.AddNode(kMarker, ToNif(a_anchor), root);
		Mesh  m = MarkerMesh(!a_lock || !a_lock->ok);
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
		if (a_lock && a_lock->ok) {
			int i = 0;
			// the lock's body on its own node: delete it to show only the keyhole
			nifly::NiNode* body = a_anchor.keyholeOnly ? nullptr : a_nif.AddNode(kLockBody, nifly::MatTransform(), node);
			for (const auto& s : a_lock->shapes) {
				if (s.body && !body) {
					continue;
				}
				if (auto* copy = a_nif.CloneShape(s.shape, std::format("VRLP_Lock{}", i++), &a_lock->nif)) {
					copy->SetTransformToParent(s.place);
					a_nif.SetParentNode(copy, s.body ? body : node);
				}
			}
		}
		return true;
	}

	int Place(const std::vector<vrlp::LockableModel>& a_list, const fs::path& a_out, Sources& a_sources, const fs::path& a_ini)
	{
		const auto saved = a_ini.empty() ? std::map<std::string, vrlp::ObjectAnchor>{} : vrlp::ParseObjectAnchors(ReadText(a_ini));
		int        placed = 0, kept = 0, missing = 0, failed = 0;
		LockModel  lock;
		LoadLock(lock, a_sources);
		for (const auto& m : a_list) {
			const std::string key = vrlp::NormalizeModelPath(m.model);
			const fs::path    out = OutPath(a_out, key);
			// a copy made before keeps where its marker is now (you may have moved it); the copy
			// itself is made again, so it gets the current marker
			std::optional<vrlp::ObjectAnchor> moved;
			if (fs::exists(out)) {
				nifly::NifFile existing;
				if (existing.Load(out) == 0) {
					moved = MarkersAnchor(existing, key);
				}
			}
			std::string source;
			const auto  file = a_sources.Get(key, source);
			if (!file) {
				std::printf("missing  %s (not in the data)\n", key.c_str());
				++missing;
				continue;
			}
			nifly::NifFile     nif;
			std::istringstream in(*file);
			if (nif.Load(in) != 0) {
				std::printf("failed   %s (could not read it from %s)\n", key.c_str(), source.c_str());
				++failed;
				continue;
			}
			const char*        from = "saved";
			vrlp::ObjectAnchor anchor;
			if (moved) {
				anchor = *moved;
				from = "kept";
				++kept;
			} else if (auto it = saved.find(key); it != saved.end()) {
				anchor = it->second;
			} else if (auto def = vrlp::DefaultObjectAnchors().find(key); def != vrlp::DefaultObjectAnchors().end()) {
				anchor = def->second;
				from = "built in";
			} else {
				anchor = vrlp::GuessObjectAnchor(m.boundMin, m.boundMax, m.door);
				from = "guessed";
			}
			StripCollision(nif);
			fs::create_directories(out.parent_path());
			if (!AddMarkers(nif, anchor, &lock) || nif.Save(out) != 0) {
				std::printf("failed   %s (could not write it)\n", key.c_str());
				++failed;
				continue;
			}
			std::printf("placed   %s (%s, %s%s, locked %d times, from %s)\n", key.c_str(), m.door ? "door" : "container", from, anchor.back ? ", both sides" : "",
				m.locked, source.c_str());
			++placed;
		}
		// the marker alone, to copy into other models
		{
			nifly::NifFile nif;
			nif.Create(nifly::NiVersion::getSSE());
			vrlp::ObjectAnchor origin;
			origin.valid = true;
			AddMarker(nif, origin, &lock);
			fs::create_directories(a_out);
			nif.Save(a_out / "VRLP_LockMarker.nif");
		}
		std::printf("\n%d placed (%d of them where their marker already was), %d missing, %d failed. Open them in NifSkope from %s\n", placed, kept, missing, failed,
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
			const auto a = MarkersAnchor(nif, key);
			if (!a) {
				std::printf("no marker %s\n", key.c_str());
				++without;
				continue;
			}
			text = vrlp::WriteObjectAnchor(text, key, *a);
			std::printf("saved    %s: %s\n", key.c_str(), vrlp::ObjectAnchorText(*a).c_str());
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
			"  lockplacer place <VRLockpicking_LockableModels.txt> <out dir> <Data folder or .bsa>[;...] [VRLockpicking_Locks.ini]\n"
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
	if (!fs::is_regular_file(argv[2])) {
		std::fprintf(stderr, "can't find %s (give its full path; the game writes it to Documents\\My Games\\Skyrim VR\\SKSE)\n", argv[2]);
		return 1;
	}
	const auto list = vrlp::ParseLockableModels(ReadText(argv[2]));
	if (list.empty()) {
		std::fprintf(stderr, "no models in %s\n", argv[2]);
		return 1;
	}
	if (command == "place") {
		Sources sources(Split(argv[4], ';'));
		return Place(list, argv[3], sources, argc > 5 ? fs::path(argv[5]) : fs::path());
	}
	if (command == "read") {
		return Read(list, argv[3], argv[4]);
	}
	return Usage();
}
