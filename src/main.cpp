#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_BITMAP_H
#include <filesystem>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <map>
#include <codecvt>


namespace fs = std::filesystem;


struct GlyphInfo {
	int offset;
	int s;
	int y;
	int w;
	int h;
};

void writeMonoBitmap(std::ofstream &cpp, uint8_t *buffer, int w, int h, int pitch) {
	cpp << std::hex;
	for (int j = 0; j < h; ++j) {
		cpp << "\t";

		// write bytes
		for (int i = 0; i < (w + 7) / 8; ++i) {
			cpp << "0x" << std::setw(2) << std::setfill('0') << int(buffer[i]) << ", ";
		}

		// write comment
		cpp << "// ";
		for (int i = 0; i < w; ++i) {
			cpp << ((buffer[i / 8] & (0x80 >> (i & 7))) != 0 ? '#' : ' ');
		}
		cpp << std::endl;

		buffer += pitch;
	}
	cpp << std::endl;
	cpp << std::dec;
}

GlyphInfo addPlaceholder(std::ofstream &cpp, int &offset) {
	GlyphInfo info;

	info.offset = offset;
	info.s = 0;
	info.y = 8;
	info.w = 7;
	info.h = 8;

	uint8_t buffer[] = {0xfe, 0xc6, 0xaa, 0x92, 0x92, 0xaa, 0xc6, 0xfe};

	cpp << "\t// placeholder" << std::endl;
	writeMonoBitmap(cpp, buffer, info.w, info.h, 1);

	offset += 8;

	return info;
}


GlyphInfo addGlyph(std::ofstream &cpp, FT_Face &face, int code, const std::string &s, int &offset) {
	GlyphInfo info;

	int error = FT_Load_Char(face, code, FT_LOAD_TARGET_MONO /*FT_LOAD_TARGET_NORMAL*/ /*| FT_LOAD_FORCE_AUTOHINT*/);
	auto glyph = face->glyph;

	// get glyph descriptor
	FT_Glyph glyphDesc;
	error = FT_Get_Glyph(glyph, &glyphDesc);
	if (glyphDesc->format == FT_GLYPH_FORMAT_OUTLINE) {
		// embolden glyph
		auto* outlineGlyph = reinterpret_cast<FT_OutlineGlyph>(glyphDesc);
		//FT_Outline_Embolden(&outlineGlyph->outline, weight);

		// render glyph
		FT_Glyph_To_Bitmap(&glyphDesc, FT_RENDER_MODE_MONO/*FT_RENDER_MODE_NORMAL*/, nullptr, 1);
		auto* bitmapGlyph = reinterpret_cast<FT_BitmapGlyph>(glyphDesc);
		FT_Bitmap& bitmap = bitmapGlyph->bitmap;

		info.offset = offset;
		info.s = s.size();
		info.y = glyph->bitmap_top;
		info.w = bitmap.width;
		info.h = bitmap.rows;

		cpp << "\t";
		cpp << std::hex;
		for (uint8_t ch : s) {
			cpp << "0x" << std::setw(2) << std::setfill('0') << int(ch) << ", ";
		}
		cpp << std::dec;
		cpp << "// ";
		if (s == " " || s == "\\")
			cpp << "'" << s << "'";
		else
			cpp << s;
		cpp << std::endl;

		uint8_t *buffer = bitmap.buffer;
		/*for (int j = 0; j < bitmap.rows; ++j) {
			// write bytes
			int byte = 0;
			int bit = 0x80;
			for (int i = 0; i < bitmap.width; ++i) {
				if (bit == 0) {
					bit = 0x80;
					cpp << "0x" << std::setw(2) << std::setfill('0') << byte << ", ";
				}

				if (buffer[i] >= 128)
					byte |= bit;
				bit >>= 1;
			}
			cpp << "0x" << std::setw(2) << std::setfill('0') << byte << ", // ";

			// write comment
			for (int i = 0; i < bitmap.width; ++i) {
				cpp << (buffer[i] >= 128 ? '#' : ' ');
			}
			cpp << std::endl;

			buffer += bitmap.pitch;
		}*/
		writeMonoBitmap(cpp, bitmap.buffer, bitmap.width, bitmap.rows, bitmap.pitch);

		offset += s.size() + bitmap.rows * ((bitmap.width + 7) / 8);
	}

	return info;
}


/*
	Usage: Label_Tool <text file>
*/
int main(int argc, char **argv) {
	if (argc < 3)
		return 1;

	// font input path
	fs::path inPath = argv[1];
	int fontSize = std::stoi(argv[2]);


	std::string name = inPath.stem().string() + std::to_string(fontSize) + "pt";

	auto hppPath = inPath.parent_path() / (name + ".hpp");
	auto cppPath = inPath.parent_path() / (name + ".cpp");


	// init freetype
	// -------------

	// https://freetype.org/freetype2/docs/tutorial/step1.html
	FT_Library library;
	auto error = FT_Init_FreeType(&library);

	FT_Face face;
	error = FT_New_Face(library, inPath.string().c_str(), 0, &face);
	if (error) {
		std::cerr << "Unable to load font" << std::endl;
		exit(1);
	}


	// set initial font height
	FT_Size_RequestRec sizeInfo{
		FT_SIZE_REQUEST_TYPE_NOMINAL,
		fontSize << 6,
		fontSize << 6,
		96,
		96
	};
	error = FT_Request_Size(face, &sizeInfo);


	std::ofstream hpp(hppPath.string(), std::ofstream::out | std::ofstream::trunc);
	std::ofstream cpp(cppPath.string(), std::ofstream::out | std::ofstream::trunc);


	std::map<std::string, GlyphInfo> glyphInfos;
	int offset = 0;

	cpp << "#include \"header.hpp\"" << std::endl;
	cpp << std::endl;

	// data
	cpp << "const uint8_t " << name << "Data[] = {" << std::endl;
	glyphInfos[std::string()] = addPlaceholder(cpp, offset);
	for (char ch = '!'; ch <= '~'; ++ch) {
		std::string s(1, ch);
		GlyphInfo info = addGlyph(cpp, face, ch, s, offset);
		glyphInfos[s] = info;
	}
	const char *chars[] = {
		"°",
		"Ä",
		"Ö",
		"Ü",
		"ß",
		"ä",
		"ö",
		"ü",
	};
	for (auto ch : chars) {
		std::string s(ch);

		std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> convert;
		int code = convert.from_bytes(s)[0];

		GlyphInfo info = addGlyph(cpp, face, code, s, offset);
		glyphInfos[s] = info;
	}
	cpp << "};" << std::endl;

	// glyphs
	int maxY = 0;
	for (auto [s, info] : glyphInfos) {
		maxY = std::max(info.y, maxY);
	}
	cpp << "const Font::Glyph " << name << "Glyphs[] = {" << std::endl;
	int height = 0;
	for (auto [s, info] : glyphInfos) {
		int y = maxY - info.y;
		cpp << "\t{" << info.offset << ", " << info.s << ", " << y << ", " << info.w << ", " << info.h << "}," << " // ";
		if (s == " " || s == "\\")
			cpp << "'" << s << "'";
		else
			cpp << s;
		cpp << std::endl;
		height = std::max(y + info.h, height);
	}
	cpp << "};" << std::endl;

	// font
	cpp << "extern const Font " << name << " = {" << std::endl;
	cpp << "\t1, 2, 5, " << height << ", " << name << "Data, " << name << "Glyphs, " << name << "Glyphs + " << glyphInfos.size() << "," << std::endl;
	cpp << "};" << std::endl;
	cpp << std::endl;
	cpp << "#include \"footer.hpp\"" << std::endl;

	// header file
	hpp << "#pragma once" << std::endl;
	hpp << std::endl;
	hpp << "#include \"header.hpp\"" << std::endl;
	hpp << std::endl;
	hpp << "extern const Font " << name << ";" << std::endl;
	hpp << std::endl;
	hpp << "#include \"footer.hpp\"" << std::endl;
}
