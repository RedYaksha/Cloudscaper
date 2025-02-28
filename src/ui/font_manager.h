#ifndef UI_FONT_MANAGER_H_ 
#define UI_FONT_MANAGER_H_

#include <string>
#include <unordered_map>

#include "artery-font/stdio-serialization.h"
#include <vector>

class ByteArray : public std::vector<unsigned char> {
public:
    using std::vector<unsigned char>::vector; // Inherit constructors

    // Implicit conversion to unsigned char*
    operator unsigned char*() {
        return this->data();
    }

    // Implicit conversion to const unsigned char*
    operator const unsigned char*() const {
        return this->data();
    }

    // Implicit conversion to void* if necessary
    operator void*() {
        return static_cast<void*>(this->data());
    }

    operator const void*() const {
        return static_cast<const void*>(this->data());
    }
};

class ByteString : public std::string {
public:
    using std::string::string; // Inherit constructors

    // Implicit conversion to char*
    operator char*() {
        return this->data();
    }

    // Implicit conversion to const char*
    operator const char*() const {
        return this->data();
    }

    // Implicit conversion to void*
    operator void*() {
        return static_cast<void*>(this->data());
    }

    operator const void*() const {
        return static_cast<const void*>(this->data());
    }
};

template <typename T>
class VectorWrapper : public std::vector<T> {
public:
    using std::vector<T>::vector; // Inherit constructors

    // Implicit conversion to T*
    operator T*() {
        return this->data();
    }

    // Implicit conversion to const T*
    operator const T*() const {
        return this->data();
    }

    // Implicit conversion to void*
    operator void*() {
        return static_cast<void*>(this->data());
    }

    operator const void*() const {
        return static_cast<const void*>(this->data());
    }
};


typedef artery_font::ArteryFont<float, VectorWrapper, ByteArray, ByteString> ArteryFont;
typedef std::string FontID;

struct FontEntry {
    ArteryFont font;
    std::unordered_map<uint32_t, artery_font::Glyph<float>> glyphMap;
    std::string arfontPath;
    std::string atlasImagePath;
    std::string id;
};

class FontManager {
public:
    FontManager() = default;

    bool RegisterFont(FontID id, std::string arfontPath, std::string atlasImagePath);
    bool GetFontEntry(FontID id, FontEntry& outFontEntry) const;
    const FontEntry& GetFontEntry(FontID id) const;
    bool ComputeTextScreenSize(FontID id, float fontSize, std::string text, float& outWidth, float& outHeight) const;
    
    


private:
    std::unordered_map<FontID, FontEntry> fontMap_;
};

#endif // UI_FONT_MANAGER_H_ 
