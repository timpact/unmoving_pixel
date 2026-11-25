# Converts a binary file into a C source with
#   const unsigned char ui_font_data[];  const unsigned long ui_font_data_size;
# Usage: cmake -DIN=<file> -DOUT=<file.c> -P embed_file.cmake
file(READ ${IN} hex HEX)
string(LENGTH "${hex}" hex_len)
math(EXPR size "${hex_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
file(WRITE ${OUT}
"/* Generated from ${IN} – do not edit. */\n"
"const unsigned char ui_font_data[] = {\n${bytes}\n};\n"
"const unsigned long ui_font_data_size = ${size};\n")
