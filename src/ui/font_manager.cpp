#include "font_manager.h"

#include "ninmath/ninmath.h"

bool FontManager::RegisterFont(FontID id, std::string arfontPath, std::string atlasImagePath) {
    if(fontMap_.contains(id)) {
        return false;
    }
    
    ArteryFont font;
    const bool success = artery_font::readFile<float, VectorWrapper, ByteArray, ByteString>(
            font, arfontPath.data());
    
    if(!success) {
        return false;
    }
    
    FontEntry entry;
    entry.id = id;
    entry.font = font;
    entry.arfontPath = arfontPath;
    entry.atlasImagePath = atlasImagePath;

    for(auto& glyph : font.variants[0].glyphs) {
        entry.glyphMap.insert({glyph.codepoint, glyph});
    }
    
    fontMap_.insert({id, entry});

    return true;
}

bool FontManager::GetFontEntry(FontID id, FontEntry& outFontEntry) const {
    if(!fontMap_.contains(id)) {
        return false;
    }

    outFontEntry = fontMap_.at(id);
    return true;
}

const FontEntry& FontManager::GetFontEntry(FontID id) const {
    return fontMap_.at(id);
}

bool FontManager::ComputeTextScreenSize(FontID id, float fontSize, std::string text, float& outWidth,
    float& outHeight) const {
    if(!fontMap_.contains(id)) {
        return false;
    }

    const FontEntry& entry = GetFontEntry(id);
    
    float curX = 0.f;
    float curY = 0.f;

    for(int i = 0; i < text.size(); i++) {
        const char& curChar = text[i];
        const artery_font::Glyph<float>& glyph = entry.glyphMap.at(curChar);
        curX += fontSize * glyph.advance.h;
    }

    // use height of l as height (since it starts at baseline and is as tall as the entire text line)
    const artery_font::Glyph<float>& glyph = entry.glyphMap.at('l');

    outHeight = glyph.planeBounds.t * fontSize - glyph.planeBounds.b * fontSize;
    outWidth = curX;

    return true;
}
