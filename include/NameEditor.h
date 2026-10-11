#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace Wheel {
    struct NameEditor {
        std::string text;
        std::size_t cursor=0,anchor=0;
        void Set(std::string value) {text=std::move(value);cursor=anchor=text.size();}
        std::size_t Prev(std::size_t at) const {
            if(!at)return 0;
            --at;while(at && (static_cast<unsigned char>(text[at])&0xc0)==0x80)--at;
            return at;
        }
        std::size_t Next(std::size_t at) const {
            if(at>=text.size())return text.size();
            ++at;while(at<text.size() && (static_cast<unsigned char>(text[at])&0xc0)==0x80)++at;
            return at;
        }
        std::size_t Begin() const {return std::min(cursor,anchor);}
        std::size_t End() const {return std::max(cursor,anchor);}
        bool Selected() const {return cursor!=anchor;}
        std::string Selection() const {return text.substr(Begin(),End()-Begin());}
        void SelectAll() {anchor=0;cursor=text.size();}
        void Place(std::size_t at,bool select=false) {cursor=std::min(at,text.size());if(!select)anchor=cursor;}
        void Move(int direction,bool select) {
            if(!select && Selected()){Place(direction<0?Begin():End());return;}
            Place(direction<0?Prev(cursor):Next(cursor),select);
        }
        void EraseSelection() {const auto first=Begin();text.erase(first,End()-first);cursor=anchor=first;}
        void Backspace() {if(!Selected())anchor=Prev(cursor);EraseSelection();}
        void Delete() {if(!Selected())anchor=Next(cursor);EraseSelection();}
        bool Insert(std::uint32_t scalar) {
            if(scalar<32 || scalar==127 || scalar>0x10ffff || (scalar>=0xd800 && scalar<=0xdfff))return false;
            std::string bytes;
            if(scalar<0x80)bytes+=static_cast<char>(scalar);
            else if(scalar<0x800){bytes+=static_cast<char>(0xc0|(scalar>>6));bytes+=static_cast<char>(0x80|(scalar&63));}
            else if(scalar<0x10000){bytes+=static_cast<char>(0xe0|(scalar>>12));bytes+=static_cast<char>(0x80|((scalar>>6)&63));bytes+=static_cast<char>(0x80|(scalar&63));}
            else {bytes+=static_cast<char>(0xf0|(scalar>>18));bytes+=static_cast<char>(0x80|((scalar>>12)&63));bytes+=static_cast<char>(0x80|((scalar>>6)&63));bytes+=static_cast<char>(0x80|(scalar&63));}
            if(text.size()-(End()-Begin())+bytes.size()>128)return false;
            EraseSelection();text.insert(cursor,bytes);cursor+=bytes.size();anchor=cursor;return true;
        }
        void InsertUtf16(std::u16string_view value) {
            for(std::size_t i=0;i<value.size();++i) {
                std::uint32_t c=value[i];
                if(c>=0xd800 && c<=0xdbff) {
                    if(i+1==value.size() || value[i+1]<0xdc00 || value[i+1]>0xdfff)continue;
                    c=0x10000+((c-0xd800)<<10)+(value[++i]-0xdc00);
                }
                Insert(c);
            }
        }
    };
}
