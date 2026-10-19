// Generates the solid-colour debug models (Meshes/VRLockpicking/*.nif) and their textures
// (Textures/VRLockpicking/*.dds) used by the debug view. Built against nifly
// (https://github.com/ousnius/nifly); see tools/nifgen/README.md.
//
// Every model is made of axis-aligned boxes in the lock frame used by the plugin:
// x = right, y = into the lock, z = up, origin = the keyhole (or the tool tip).

#include <NifFile.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace nifly;

namespace
{
	struct Box
	{
		Vector3 lo;
		Vector3 hi;
	};

	struct Colour
	{
		std::string   name;
		std::uint8_t r, g, b;
	};

	void AddBox(const Box& a_box, std::vector<Vector3>& v, std::vector<Triangle>& t, std::vector<Vector2>& uv, std::vector<Vector3>& n)
	{
		const Vector3& a = a_box.lo;
		const Vector3& b = a_box.hi;
		// 6 faces, 4 vertices each (flat normals), counter-clockwise seen from outside
		const std::array<std::array<Vector3, 4>, 6> faces{ {
			{ Vector3(b.x, a.y, a.z), Vector3(b.x, b.y, a.z), Vector3(b.x, b.y, b.z), Vector3(b.x, a.y, b.z) },  // +x
			{ Vector3(a.x, b.y, a.z), Vector3(a.x, a.y, a.z), Vector3(a.x, a.y, b.z), Vector3(a.x, b.y, b.z) },  // -x
			{ Vector3(b.x, b.y, a.z), Vector3(a.x, b.y, a.z), Vector3(a.x, b.y, b.z), Vector3(b.x, b.y, b.z) },  // +y
			{ Vector3(a.x, a.y, a.z), Vector3(b.x, a.y, a.z), Vector3(b.x, a.y, b.z), Vector3(a.x, a.y, b.z) },  // -y
			{ Vector3(a.x, a.y, b.z), Vector3(b.x, a.y, b.z), Vector3(b.x, b.y, b.z), Vector3(a.x, b.y, b.z) },  // +z
			{ Vector3(a.x, b.y, a.z), Vector3(b.x, b.y, a.z), Vector3(b.x, a.y, a.z), Vector3(a.x, a.y, a.z) },  // -z
		} };
		const std::array<Vector3, 6> normals{ Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, -1, 0), Vector3(0, 0, 1), Vector3(0, 0, -1) };
		for (int f = 0; f < 6; ++f) {
			const auto base = static_cast<std::uint16_t>(v.size());
			for (int k = 0; k < 4; ++k) {
				v.push_back(faces[f][k]);
				n.push_back(normals[f]);
				uv.emplace_back(k == 1 || k == 2 ? 1.0f : 0.0f, k >= 2 ? 0.0f : 1.0f);
			}
			t.emplace_back(base, base + 1, base + 2);
			t.emplace_back(base, base + 2, base + 3);
		}
	}

	// A cylinder along +Y (the keyhole axis), from a_y0 to a_y1, with end caps; smooth sides.
	void AddCylinder(float a_radius, float a_y0, float a_y1, int a_segments, std::vector<Vector3>& v, std::vector<Triangle>& t, std::vector<Vector2>& uv,
		std::vector<Vector3>& n)
	{
		constexpr float kTau = 6.28318530718f;
		// sides
		const auto side = static_cast<std::uint16_t>(v.size());
		for (int i = 0; i <= a_segments; ++i) {
			const float   a = kTau * static_cast<float>(i) / static_cast<float>(a_segments);
			const Vector3 dir(std::sin(a), 0.0f, std::cos(a));
			for (const float y : { a_y0, a_y1 }) {
				v.emplace_back(dir.x * a_radius, y, dir.z * a_radius);
				n.push_back(dir);
				uv.emplace_back(static_cast<float>(i) / static_cast<float>(a_segments), y == a_y0 ? 1.0f : 0.0f);
			}
		}
		for (int i = 0; i < a_segments; ++i) {
			const auto k = static_cast<std::uint16_t>(side + i * 2);
			t.emplace_back(k, k + 1, k + 3);
			t.emplace_back(k, k + 3, k + 2);
		}
		// caps
		for (const float y : { a_y0, a_y1 }) {
			const Vector3 normal(0.0f, y == a_y0 ? -1.0f : 1.0f, 0.0f);
			const auto    centre = static_cast<std::uint16_t>(v.size());
			v.emplace_back(0.0f, y, 0.0f);
			n.push_back(normal);
			uv.emplace_back(0.5f, 0.5f);
			for (int i = 0; i < a_segments; ++i) {
				const float a = kTau * static_cast<float>(i) / static_cast<float>(a_segments);
				v.emplace_back(std::sin(a) * a_radius, y, std::cos(a) * a_radius);
				n.push_back(normal);
				uv.emplace_back(0.5f + 0.5f * std::sin(a), 0.5f + 0.5f * std::cos(a));
			}
			for (int i = 0; i < a_segments; ++i) {
				t.emplace_back(centre, static_cast<std::uint16_t>(centre + 1 + i), static_cast<std::uint16_t>(centre + 1 + (i + 1) % a_segments));
			}
		}
	}

	// A UV sphere around the origin.
	void AddSphere(float a_radius, int a_rings, int a_segments, std::vector<Vector3>& v, std::vector<Triangle>& t, std::vector<Vector2>& uv,
		std::vector<Vector3>& n)
	{
		constexpr float kPi = 3.14159265359f;
		const auto      base = static_cast<std::uint16_t>(v.size());
		for (int r = 0; r <= a_rings; ++r) {
			const float phi = kPi * static_cast<float>(r) / static_cast<float>(a_rings);
			for (int s = 0; s <= a_segments; ++s) {
				const float   theta = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(a_segments);
				const Vector3 dir(std::sin(phi) * std::cos(theta), std::sin(phi) * std::sin(theta), std::cos(phi));
				v.emplace_back(dir.x * a_radius, dir.y * a_radius, dir.z * a_radius);
				n.push_back(dir);
				uv.emplace_back(static_cast<float>(s) / static_cast<float>(a_segments), static_cast<float>(r) / static_cast<float>(a_rings));
			}
		}
		const int row = a_segments + 1;
		for (int r = 0; r < a_rings; ++r) {
			for (int s = 0; s < a_segments; ++s) {
				const auto a = static_cast<std::uint16_t>(base + r * row + s);
				const auto b = static_cast<std::uint16_t>(a + row);
				t.emplace_back(a, b, static_cast<std::uint16_t>(b + 1));
				t.emplace_back(a, static_cast<std::uint16_t>(b + 1), static_cast<std::uint16_t>(a + 1));
			}
		}
	}

	// A thin tube along a circular arc on the lock face (the XZ plane): radius a_radius around
	// the origin, from a_deg0 to a_deg1 (0 = up, + = clockwise as seen from -Y), tube radius a_tube.
	void AddArcTube(float a_radius, float a_tube, float a_deg0, float a_deg1, int a_steps, int a_sides, std::vector<Vector3>& v,
		std::vector<Triangle>& t, std::vector<Vector2>& uv, std::vector<Vector3>& n)
	{
		constexpr float kPi = 3.14159265359f;
		const auto      base = static_cast<std::uint16_t>(v.size());
		for (int i = 0; i <= a_steps; ++i) {
			const float   a = (a_deg0 + (a_deg1 - a_deg0) * static_cast<float>(i) / static_cast<float>(a_steps)) * kPi / 180.0f;
			const Vector3 out(std::sin(a), 0.0f, std::cos(a));  // from the centre outwards
			for (int k = 0; k <= a_sides; ++k) {
				const float   b = 2.0f * kPi * static_cast<float>(k) / static_cast<float>(a_sides);
				const Vector3 dir(out.x * std::cos(b), std::sin(b), out.z * std::cos(b));
				v.emplace_back(out.x * a_radius + dir.x * a_tube, dir.y * a_tube, out.z * a_radius + dir.z * a_tube);
				n.push_back(dir);
				uv.emplace_back(static_cast<float>(i) / static_cast<float>(a_steps), static_cast<float>(k) / static_cast<float>(a_sides));
			}
		}
		const int row = a_sides + 1;
		for (int i = 0; i < a_steps; ++i) {
			for (int k = 0; k < a_sides; ++k) {
				const auto p = static_cast<std::uint16_t>(base + i * row + k);
				const auto q = static_cast<std::uint16_t>(p + row);
				t.emplace_back(p, q, static_cast<std::uint16_t>(q + 1));
				t.emplace_back(p, static_cast<std::uint16_t>(q + 1), static_cast<std::uint16_t>(p + 1));
			}
		}
	}

	void WriteDds(const std::filesystem::path& a_path, std::uint8_t r, std::uint8_t g, std::uint8_t b)
	{
		// 4x4 uncompressed B8G8R8A8, one mip level
		std::uint32_t hdr[31]{};
		hdr[0] = 124;                             // size
		hdr[1] = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000;  // caps | height | width | pitch | pixelformat
		hdr[2] = 4;                               // height
		hdr[3] = 4;                               // width
		hdr[4] = 16;                              // pitch
		hdr[6] = 1;                               // mip count
		hdr[18] = 32;                             // pixel format size
		hdr[19] = 0x41;                           // RGB | ALPHAPIXELS
		hdr[21] = 32;                             // bits
		hdr[22] = 0x00FF0000;
		hdr[23] = 0x0000FF00;
		hdr[24] = 0x000000FF;
		hdr[25] = 0xFF000000;
		hdr[26] = 0x1000;  // caps: texture
		std::ofstream out(a_path, std::ios::binary);
		out.write("DDS ", 4);
		out.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));
		for (int i = 0; i < 16; ++i) {
			const std::uint8_t px[4]{ b, g, r, 255 };
			out.write(reinterpret_cast<const char*>(px), 4);
		}
	}

	struct Mesh
	{
		std::vector<Vector3>  v;
		std::vector<Triangle> t;
		std::vector<Vector2>  uv;
		std::vector<Vector3>  n;
	};

	// a_alpha < 1: see-through (alpha blended)
	void WriteMesh(const std::filesystem::path& a_path, Mesh& a_mesh, const Colour& a_colour, float a_alpha = 1.0f)
	{
		auto& v = a_mesh.v;
		auto& t = a_mesh.t;
		auto& uv = a_mesh.uv;
		auto& n = a_mesh.n;
		NifFile nif;
		nif.Create(NiVersion::getSSE());
		NiShape* shape = nif.CreateShapeFromData(a_path.stem().string(), &v, &t, &uv, &n);
		auto*    shader = dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(shape));
		if (shader) {
			// own emittance in the same colour: readable in the dark, no specular shine
			shader->shaderFlags1 |= SLSF1_OWN_EMIT;
			shader->shaderFlags1 &= ~SLSF1_SPECULAR;
			shader->shaderFlags1 &= ~SLSF1_CAST_SHADOWS;
			shader->shaderFlags2 |= SLSF2_DOUBLE_SIDED;  // visible from any side, whatever the winding
			shader->emissiveColor = Vector3(a_colour.r / 255.0f, a_colour.g / 255.0f, a_colour.b / 255.0f);
			shader->emissiveMultiple = 0.6f;
			shader->alpha = a_alpha;
		}
		if (a_alpha < 1.0f) {
			auto alpha = std::make_unique<NiAlphaProperty>();
			alpha->flags = 4333;  // blend: source alpha / one minus source alpha, no sorting
			nif.AssignAlphaProperty(shape, std::move(alpha));
		}
		std::string diffuse = "textures\\VRLockpicking\\" + a_colour.name + ".dds";
		std::string normal = "textures\\VRLockpicking\\flat_n.dds";
		nif.SetTextureSlot(shape, diffuse, 0);
		nif.SetTextureSlot(shape, normal, 1);
		shape->UpdateBounds();
		if (nif.Save(a_path) != 0) {
			std::fprintf(stderr, "failed to save %s\n", a_path.string().c_str());
		}
	}

	void WriteNif(const std::filesystem::path& a_path, const std::vector<Box>& a_boxes, const Colour& a_colour)
	{
		Mesh m;
		for (const auto& box : a_boxes) {
			AddBox(box, m.v, m.t, m.uv, m.n);
		}
		WriteMesh(a_path, m, a_colour);
	}

	Box Cube(float a_half) { return { Vector3(-a_half, -a_half, -a_half), Vector3(a_half, a_half, a_half) }; }
}

int main(int argc, char** argv)
{
	if (argc != 2) {
		std::fprintf(stderr, "usage: nifgen <output Data dir>\n");
		return 2;
	}
	const std::filesystem::path out = argv[1];
	const auto                  meshes = out / "Meshes" / "VRLockpicking";
	const auto                  textures = out / "Textures" / "VRLockpicking";
	std::filesystem::create_directories(meshes);
	std::filesystem::create_directories(textures);

	const Colour slate{ "slate", 70, 90, 140 }, yellow{ "yellow", 235, 190, 40 }, white{ "white", 240, 240, 240 },
		red{ "red", 230, 40, 40 }, green{ "green", 40, 220, 70 }, magenta{ "magenta", 220, 50, 220 },
		gray{ "gray", 130, 130, 130 }, lime{ "lime", 170, 255, 60 }, cyan{ "cyan", 40, 210, 240 }, orange{ "orange", 255, 140, 30 };
	for (const auto& c : { slate, yellow, white, red, green, magenta, gray, lime, cyan, orange }) {
		WriteDds(textures / (c.name + ".dds"), c.r, c.g, c.b);
	}
	WriteDds(textures / "flat_n.dds", 128, 128, 255);

	// Lock, sized for a face half-size of 8 (fLockMeshRadius) - the plugin scales uniformly.
	WriteNif(meshes / "dbg_lock.nif", { { Vector3(-8, 0.6f, -8), Vector3(8, 3.6f, 8) } }, slate);
	WriteNif(meshes / "dbg_cylinder.nif", { { Vector3(-3.2f, 0.1f, -3.2f), Vector3(3.2f, 0.7f, 3.2f) } }, yellow);
	WriteNif(meshes / "dbg_keyhole.nif", { { Vector3(-0.35f, -0.25f, -1.7f), Vector3(0.35f, 0.25f, 1.7f) } }, red);
	// Tools, 16 long (fToolMeshLength), tip at the origin, handle along -y.
	// the pick's hook points along +x (in the lock: to the counter-clockwise side of the shaft)
	WriteNif(meshes / "dbg_pick.nif",
		{ { Vector3(-0.2f, -16, -0.2f), Vector3(0.2f, 0, 0.2f) }, { Vector3(0.2f, -0.5f, -0.2f), Vector3(1.0f, 0, 0.2f) },
			{ Vector3(-0.5f, -16, -0.5f), Vector3(0.5f, -12, 0.5f) } },
		cyan);
	// the wrench is a flat blade: wide along x, thin along z (its flat side faces +z)
	WriteNif(meshes / "dbg_wrench.nif",
		{ { Vector3(-0.6f, -16, -0.12f), Vector3(0.6f, 0, 0.12f) }, { Vector3(-0.8f, -16, -0.4f), Vector3(0.8f, -12, 0.4f) },
			{ Vector3(-0.15f, -14, 0.12f), Vector3(0.15f, -2, 0.3f) } },  // a ridge on the flat side, so you can see which way it faces
		orange);
	// Laser for the model markup tool: 100 long along +y from the origin, scaled at runtime.
	// the zone a tool tip has to reach to go in: radius 1 (scaled to fInsertRadius), from just in
	// front of the keyhole to 0.6 deep (fMaxDepth 3 at the default radius 5), see-through
	{
		Mesh zone;
		AddCylinder(1.0f, -0.04f, 0.6f, 32, zone.v, zone.t, zone.uv, zone.n);
		WriteMesh(meshes / "dbg_zone.nif", zone, green, 0.35f);
	}
	// assist ring: a thin see-through arc along the pick's range (radius 6, the right half), and
	// small beads (radius 1, scaled at runtime) for the pick and the sweet spot
	{
		Mesh arc;
		AddArcTube(6.0f, 0.07f, 0.0f, 180.0f, 72, 8, arc.v, arc.t, arc.uv, arc.n);
		WriteMesh(meshes / "ring_arc.nif", arc, white, 0.45f);
		for (const auto& c : { cyan, lime, yellow }) {
			Mesh bead;
			AddSphere(1.0f, 8, 12, bead.v, bead.t, bead.uv, bead.n);
			WriteMesh(meshes / ("ring_bead_" + c.name + ".nif"), bead, c);
		}
	}
	// One segment of the lockpick counter's digits: 1 long (along X), thin, flat to the face.
	WriteNif(meshes / "count_seg.nif", { { Vector3(-0.5f, -0.04f, -0.07f), Vector3(0.5f, 0.04f, 0.07f) } }, white);
	// Unit markers (half size 0.5).
	for (const auto& c : { white, red, yellow, green, magenta, gray, lime, cyan, orange }) {
		WriteNif(meshes / ("dbg_" + c.name + ".nif"), { Cube(0.5f) }, c);
	}
	std::printf("wrote debug models to %s\n", out.string().c_str());
	return 0;
}
