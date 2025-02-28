#ifndef UI_WIDGETS_NUMERIC_INPUT_H_
#define UI_WIDGETS_NUMERIC_INPUT_H_

#include <optional>
#include <stdexcept>

#include "text_input.h"

template<typename T>
concept IsNumber = requires {
    std::is_arithmetic<T>();
};

template<typename T>
concept IsFloatingPoint = requires {
    std::is_floating_point_v<T>();
};

template<typename T>
concept IsIntegral = requires {
    std::is_integral_v<T>();
};

namespace numeric_helpers {
    template <IsNumber T>
    bool StringToNumber(const std::string& str, T& outVal);

#define TEMPL_SPECIALIZATION_STRING_TO_NUMBER(type, funcName) \
template <> inline bool StringToNumber<type>(const std::string& str, type& outVal) { \
    try { \
        outVal = std:: ## funcName ## (str); \
        return true; \
    } \
    catch (std::invalid_argument const& ex) \
    { \
        return false; \
    } \
    catch (std::out_of_range const& ex) \
    { \
        return false; \
    } \
} \

    TEMPL_SPECIALIZATION_STRING_TO_NUMBER(int, stoi)
    TEMPL_SPECIALIZATION_STRING_TO_NUMBER(long, stol)
    TEMPL_SPECIALIZATION_STRING_TO_NUMBER(unsigned long, stoul)
    TEMPL_SPECIALIZATION_STRING_TO_NUMBER(unsigned long long, stoull)
    TEMPL_SPECIALIZATION_STRING_TO_NUMBER(float, stof)
    TEMPL_SPECIALIZATION_STRING_TO_NUMBER(double, stold)
#undef TEMPL_SPECIALIZATION_STRING_TO_NUMBER
}

template<IsNumber T>
class NumericInput : public TextInput {
public:
    NumericInput(T& val)
        : value_(val) {
        padding_ = {2, 2, 6, 6};
        
    }

    void OnInitialized() override {
        valueText_ = GetStringFromValue();
        text_ = valueText_;
        TextInput::OnInitialized();
    }
    
    void OnKeyPressed(KeyEvent e) override;
    void OnFocused() override;
    void OnUnfocused() override;

    bool TrySetValueFromText(const std::string& str);
    std::string GetStringFromValue() const {
        std::string str = std::to_string(value_);

        //
        // https://stackoverflow.com/a/57883193
        // Ensure that there is a decimal point somewhere (there should be)
        if(str.find('.') != std::string::npos)
        {
            // Remove trailing zeroes
            str = str.substr(0, str.find_last_not_of('0')+1);
            // If the decimal point is now the last character, remove that as well
            if(str.find('.') == str.size()-1)
            {
                str = str.substr(0, str.size()-1);
            }
        }
        
        return str;
    }

private:
    T& value_;
    
    std::string inputText_;
    std::string valueText_;
};


template <IsNumber T>
void NumericInput<T>::OnKeyPressed(KeyEvent e) {
    if(!isFocused_) {
        return;
    }

    // delete or backspace
    if(e.key == VK_DELETE || e.key == VK_BACK) {
        if(text_.size() <= 0) {
            return;
        }
        
        text_.pop_back();
        ComputeTextSize();
        return;
    }

    // enter key
    if(e.key == VK_RETURN) {
        SetIsFocused(false);
        OnUnfocused();
        return;
    }

    char c = VirtualKeyToChar(e.key);

    if(c < '0' && c > '9' && c != '.' && c != '-') {
        return;
    }
    
    text_ += c;
    ComputeTextSize();
}

template <IsNumber T>
void NumericInput<T>::OnFocused() {
    TextInput::OnFocused();

    inputText_ = valueText_;
    SetText(inputText_);
}

template <IsNumber T>
void NumericInput<T>::OnUnfocused() {
    TextInput::OnUnfocused();

    if(TrySetValueFromText(text_)) {
        valueText_ = GetStringFromValue();
        
        SetText(valueText_);
    }
    else {
        // invalid conversion, fallback to previous value
        SetText(valueText_);
    }
}

template <IsNumber T>
bool NumericInput<T>::TrySetValueFromText(const std::string& str) {
    T val;
    bool success = numeric_helpers::StringToNumber<T>(str, val);
    if(success) {
        value_ = val;
    }
    return success;
}
#endif // UI_WIDGETS_NUMERIC_INPUT_H_
