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
#include <ranges>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// todo:
// https://steamcdn-a.akamaihd.net/apps/valve/2007/SIGGRAPH2007_AlphaTestedMagnification.pdf
// https://github.com/ShoYamanishi/SDFont


namespace fs = std::filesystem;

constexpr char INDENT[] = "    ";


struct GlyphInfo {
    GlyphInfo(std::string_view text, bool printable = true) : text(text), printable(printable) {}

    // text that is represented by this glyph (single character, utf-8 sequence, ligature)
    std::string text;

    bool printable;

    // text as code point or offset into text data
    int t;

    // offset of glyph data in data array
    int offset;

    // top y coordinate of glyph
    int y;

    // size of glyph
    int w;
    int h;

    // glyph data
    std::vector<uint8_t> data;
};

int toInt(const std::string &str) {
    int s = 0;
    for (char ch : str | std::views::reverse) {
        s <<= 8;
        s |= uint8_t(ch);
    }
    return s;
}


/**
 * Add monochome placeholder that is inserted for unknown characters
 */
GlyphInfo addMonoPlaceholder(GlyphInfo &info, int w, int h) {
    info.y = h;
    info.w = w;
    info.h = h;

    int m = w / 2;

    int pitch = (w + 7) / 8;
    info.data.resize(pitch * h);
    for (int j = 0; j < h; ++j) {
        uint8_t *row = info.data.data() + j * pitch;
        for (int i = 0; i < w; ++i) {
            int x = std::min(i, w - 1 - i);
            int y = std::min(j, h - 1 - j);

            bool border = x == 0 || y == 0;
            bool cross = w >= 7 && (y == x || (y > x && x == m));

            if (border || cross)
                row[i / 8] |= 0x80 >> (i & 7);
        }
    }

    return info;
}

GlyphInfo addGrayPlaceholder(GlyphInfo &info, int w, int h) {
    info.y = h;
    info.w = w;
    info.h = h;

    int m = w / 2;

    info.data.resize(w * h);
    for (int j = 0; j < h; ++j) {
        uint8_t *row = info.data.data() + j * w;
        for (int i = 0; i < w; ++i) {
            int x = std::min(i, w - 1 - i);
            int y = std::min(j, h - 1 - j);

            bool border = x == 0 || y == 0;
            bool cross = w >= 7 && (y == x || (y > x && x == m));

            row[i] = (border || cross) ? 255 : 0;
        }
    }

    return info;
}


/**
 * Set glyph data
 */
GlyphInfo setGlyph(GlyphInfo &info, FT_Face &face, int code, FT_Render_Mode mode) {
    int error = FT_Load_Char(face, code, FT_LOAD_TARGET_(mode) /*| FT_LOAD_FORCE_AUTOHINT*/);
    auto glyph = face->glyph;

    // get glyph descriptor
    FT_Glyph glyphDesc;
    error = FT_Get_Glyph(glyph, &glyphDesc);
    if (glyphDesc->format == FT_GLYPH_FORMAT_OUTLINE) {
        // embolden glyph
        auto* outlineGlyph = reinterpret_cast<FT_OutlineGlyph>(glyphDesc);
        //FT_Outline_Embolden(&outlineGlyph->outline, weight);

        // render glyph
        FT_Glyph_To_Bitmap(&glyphDesc, mode, nullptr, 1);
        auto* bitmapGlyph = reinterpret_cast<FT_BitmapGlyph>(glyphDesc);
        FT_Bitmap& bitmap = bitmapGlyph->bitmap;

        info.y = glyph->bitmap_top;
        info.w = bitmap.width;
        info.h = bitmap.rows;

        auto buffer = bitmap.buffer;
        int w = mode == FT_RENDER_MODE_MONO ? (bitmap.width + 7) / 8 : bitmap.width;
        for (int i = 0; i < bitmap.rows; ++i) {
            info.data.insert(info.data.end(), buffer, buffer + w);
            buffer += bitmap.pitch;
        }
    }

    return info;
}


/**
 * Write monochrome glyph data as hex bytes
 */
void writeMonoData1D(std::ofstream &cpp, GlyphInfo &info, int &offset) {
    cpp << std::hex;
    int w = (info.w + 7) / 8;
    auto buffer = info.data.data();
    for (int j = 0; j < info.h; ++j) {
        cpp << INDENT;

        // write row
        for (int i = 0; i < w; ++i) {
            cpp << "0x" << std::setw(2) << std::setfill('0') << int(buffer[i]) << ", ";
        }

        // write glyph as comment
        cpp << "// ";
        for (int i = 0; i < info.w; ++i) {
            cpp << ((buffer[i / 8] & (0x80 >> (i & 7))) != 0 ? '#' : ' ');
        }
        cpp << std::endl;

        buffer += w;
    }
    cpp << std::endl;
    cpp << std::dec;

    info.offset = offset;
    offset += info.data.size();
}

/**
 * Write gray glyph data as hex bytes
 */
void writeGrayData1D(std::ofstream &cpp, GlyphInfo &info, int &offset) {
    cpp << std::hex;
    auto buffer = info.data.data();
    for (int j = 0; j < info.h; ++j) {
        cpp << INDENT;

        // write row
        for (int i = 0; i < info.w; ++i) {
            cpp << "0x" << std::setw(2) << std::setfill('0') << int(buffer[i]) << ", ";
        }

        // write glyph as comment
        cpp << "// ";
        for (int i = 0; i < info.w; ++i) {
            cpp << ((buffer[i] & 0x80) != 0 ? '#' : ' ');
        }
        cpp << std::endl;

        buffer += info.w;
    }
    cpp << std::endl;
    cpp << std::dec;

    info.offset = offset;
    offset += info.data.size();
}


struct int2 {
    int x;
    int y;
};

void addGrayData2D(std::vector<uint8_t> &texture, int width, GlyphInfo &info, int2 &offset, int& rowHeight) {
    if (offset.x + info.w + 1 > width) {
        offset.x = 1;
        offset.y += rowHeight + 1;
        rowHeight = 0;
    }
    for (int j = 0; j < info.h; ++j) {
        auto src = info.data.begin() + j * info.w;
        auto dst = texture.begin() + (offset.y + j) * width + offset.x;
        std::copy(src, src + info.w, dst);
    }

    info.offset = offset.x | (offset.y << 12);
    offset.x += info.w + 1;
    rowHeight = std::max(rowHeight, info.h);
}

void writeGrayData2D(std::ofstream &cpp, const std::vector<uint8_t> &texture, int w, int h) {
    cpp << std::hex;
    auto buffer = texture.data();
    for (int j = 0; j < h; ++j) {
        cpp << INDENT;

        // write row
        for (int i = 0; i < w; ++i) {
            cpp << "0x" << std::setw(2) << std::setfill('0') << int(buffer[i]) << ", ";
        }

        // write row as comment
        cpp << "// ";
        for (int i = 0; i < w; ++i) {
            cpp << ((buffer[i] & 0x80) != 0 ? '#' : ' ');
        }
        cpp << std::endl;

        buffer += w;
    }
    cpp << std::endl;
    cpp << std::dec;
}


/*
    Usage: Label_Tool <text file>
*/
int main(int argc, char **argv) {
    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, nullptr, _IOFBF, 1000);
    //std::cout << "Ω" << std::endl;
    if (argc < 2)
        return 1;

    // font input path
    fs::path inPath = argv[1];

    // arguments
    int fontSize = 8;
    FT_Render_Mode mode = FT_RENDER_MODE_MONO;
    bool tex = false;
    for (int i = 2; i < argc; ++i) {
        auto arg = std::string_view(argv[i]);
        if (arg.ends_with("pt"))
            fontSize = std::stoi(std::string(arg.substr(0, arg.size() - 2)));
        else if (arg == "8bpp")
            mode = FT_RENDER_MODE_NORMAL;
        else if (arg == "tex")
            tex = true;
    }

    // construct name
    std::string name = inPath.stem().string() + std::to_string(fontSize) + "pt";
    if (mode == FT_RENDER_MODE_MONO)
        name += "1bpp";
    else
        name += "8bpp";

    // construct output file paths
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


    // collect glyphs
    std::vector<GlyphInfo> glyphInfos;
    glyphInfos.emplace_back(std::string());
    const char *spaces[] = {
        " "
    };
    for (auto ch : spaces) {
        glyphInfos.emplace_back(ch, false);
    }
    for (char ch = '!'; ch <= '~'; ++ch) {
        std::string s(1, ch);
        glyphInfos.emplace_back(s);
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
        "Ω",
        "μ",
    };
    for (auto ch : chars) {
        glyphInfos.emplace_back(ch);
    }

    // get glyph data
    int heightl = 0;
    for (auto &info : glyphInfos) {
        if (!info.text.empty()) {
            // get character code from utf-8 string
            std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> convert;
            int code = convert.from_bytes(info.text)[0];

            if (info.printable) {
                // printable glyph
                setGlyph(info, face, code, mode);
                info.t = code;

                if (code == 'l')
                    heightl = info.h;

                // print to console
                std::cout << code << ' ';
                //std::cout << info.text;
            } else {
                // non-printable
                info.t = code;
                info.offset = 0xffffff;
                info.y = 0;
                info.w = fontSize / 10 + 1;
                info.h = 0;
            }
        }
    }
    std::cout << std::endl;

    // generate placeholder glyph for unknown characters
    {
        auto &info = glyphInfos.front();
        int w = heightl * 2 / 3 | 1;
        if (mode == FT_RENDER_MODE_MONO)
            addMonoPlaceholder(info, w, heightl);
        else
            addGrayPlaceholder(info, w, heightl);
        info.t = 0;
    }


    // determine texture width
    int pixelHeight = sizeInfo.vertResolution * sizeInfo.height / 72 >> 6;
    int pixelCount = pixelHeight * pixelHeight * glyphInfos.size();
    int textureWidth = int(std::sqrt(pixelCount) + 3) & ~3;
    int textureHeight = pixelCount / textureWidth * 2;

    // generate cpp file
    cpp << "#include \"header.hpp\"" << std::endl;
    cpp << std::endl;

    // write bitmap data for all glyphs
    cpp << "const uint8_t " << name << "Data[] = {" << std::endl;
    int dataSize;
    if (!tex) {
        // linear data
        int offset = 0;
        for (auto &info : glyphInfos) {
            if (info.printable) {
                if (mode == FT_RENDER_MODE_MONO)
                    writeMonoData1D(cpp, info, offset);
                else
                    writeGrayData1D(cpp, info, offset);
            }
        }
        dataSize = offset;
    } else {
        // texture (2D) data
        std::vector<uint8_t> texture;
        int2 offset = {1, 1};
        int rowHeight = 0;
        texture.resize(textureWidth * textureHeight);
        for (auto &info : glyphInfos) {
            if (info.printable) {
                addGrayData2D(texture, textureWidth, info, offset, rowHeight);
            }
        }
        int textureHeight = offset.y + rowHeight + 1;
        writeGrayData2D(cpp, texture, textureWidth, textureHeight);
        dataSize = textureWidth | (textureHeight << 16);
    }
    cpp << "};" << std::endl;

    // determine maximum y coordinate of all glyphs
    int maxY = 0;
    for (auto &info : glyphInfos) {
        maxY = std::max(info.y, maxY);
    }

    // write Glyph structures
    cpp << "const Vector2<uint32_t> " << name << "Glyphs[] = {" << std::endl;
    int height = 0;
    for (auto &info : glyphInfos) {
        int y = maxY - info.y;

		// t (glyph text): 18 bit
		// w : 7 bit
		// h : 7 bit
		uint32_t infoX = info.t | (info.w << 18) | (info.h << 25);

		// offset: 24 bit
		// y : 7 bit
		// flag (glyph code or offset): 1 bit
		uint32_t infoY = info.offset | (y << 24);

        cpp << INDENT << "{" << infoX << ", " << infoY << "},";

        // text represented by the glyph as comment
        cpp << " // ";
        if (info.text == " " || info.text == "\\")
            cpp << "'" << info.text << "'";
        else
            cpp << info.text;
        cpp << std::endl;

        height = std::max(y + info.h, height);
    }
    cpp << "};" << std::endl;

    // write font
    int gapWidth = fontSize >= 10 ? 2 : 1;
    cpp << "extern const Font " << name << " = {" << std::endl;
    {
        //cpp << INDENT << "1, 2, 5, " << std::endl; // gapWidth, spaceWidth, tabWidth
        cpp << INDENT << gapWidth << "," << std::endl;
        cpp << INDENT << height << "," << std::endl;
        cpp << INDENT << name << "Data, " << dataSize << "," << std::endl;
        cpp << INDENT << name << "Glyphs, " << name << "Glyphs + " << glyphInfos.size() << "," << std::endl;
    }
    cpp << "};" << std::endl;
    cpp << std::endl;
    cpp << "#include \"footer.hpp\"" << std::endl;

    // generate header file
    hpp << "#pragma once" << std::endl;
    hpp << std::endl;
    hpp << "#include \"header.hpp\"" << std::endl;
    hpp << std::endl;
    hpp << "extern const Font " << name << ";" << std::endl;
    hpp << std::endl;
    hpp << "#include \"footer.hpp\"" << std::endl;
}
