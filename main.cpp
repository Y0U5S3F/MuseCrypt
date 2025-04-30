#include <stdio.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <iomanip>
#include <cstdint>

extern "C" {
    #include <png.h>
}

void write_midi_file(const std::string& filename, const std::vector<uint8_t>& midi_notes) {
    std::ofstream out(filename, std::ios::binary);

    // Header chunk: MThd + size + format + ntrks + division
    uint8_t header[] = {
        'M','T','h','d',  // Chunk type
        0x00,0x00,0x00,0x06,  // Header length = 6
        0x00,0x00,  // Format 0
        0x00,0x01,  // One track
        0x01,0xE0   // 480 ticks per quarter note
    };
    out.write(reinterpret_cast<char*>(header), sizeof(header));

    std::vector<uint8_t> track_data;

    // Add a tempo event (optional but recommended)
    uint8_t tempo_event[] = {
        0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20  // Tempo: 500000 µs per quarter note (120 BPM)
    };
    track_data.insert(track_data.end(), tempo_event, tempo_event + sizeof(tempo_event));

    // Note-on and note-off events for each MIDI byte pair
    for (size_t i = 0; i < midi_notes.size(); ++i) {
        uint8_t note = midi_notes[i] % 128;
        // Note on
        track_data.push_back(0x00);               // delta-time
        track_data.push_back(0x90);               // note-on, channel 0
        track_data.push_back(note);               // note number
        track_data.push_back(0x64);               // velocity

        // Note off after a short delay
        track_data.push_back(0x10);               // small delta-time
        track_data.push_back(0x80);               // note-off, channel 0
        track_data.push_back(note);               // note number
        track_data.push_back(0x40);               // velocity
    }

    // End-of-track meta event
    uint8_t end_event[] = { 0x00, 0xFF, 0x2F, 0x00 };
    track_data.insert(track_data.end(), end_event, end_event + sizeof(end_event));

    // Track chunk header
    uint32_t track_size = static_cast<uint32_t>(track_data.size());
    uint8_t track_header[] = {
        'M','T','r','k',
        static_cast<uint8_t>((track_size >> 24) & 0xFF),
        static_cast<uint8_t>((track_size >> 16) & 0xFF),
        static_cast<uint8_t>((track_size >> 8) & 0xFF),
        static_cast<uint8_t>(track_size & 0xFF)
    };

    out.write(reinterpret_cast<char*>(track_header), sizeof(track_header));
    out.write(reinterpret_cast<char*>(track_data.data()), track_data.size());
    out.close();
    std::cout << "MIDI file written to: " << filename << std::endl;
}

void read_png_binary(const char* filename, const char* midi_output) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) throw std::runtime_error("Failed to open file");

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) throw std::runtime_error("png_create_read_struct failed");

    png_infop info = png_create_info_struct(png);
    if (!info) throw std::runtime_error("png_create_info_struct failed");

    if (setjmp(png_jmpbuf(png))) throw std::runtime_error("Error during init_io");

    png_init_io(png, fp);
    png_read_info(png, info);

    int width  = png_get_image_width(png, info);
    int height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth  = png_get_bit_depth(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE) png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    std::vector<png_bytep> row_pointers(height);
    std::vector<unsigned char> img_data(width * height * 4);
    for (int y = 0; y < height; y++)
        row_pointers[y] = &img_data[y * width * 4];

    png_read_image(png, row_pointers.data());
    fclose(fp);
    png_destroy_read_struct(&png, &info, nullptr);

    std::vector<uint8_t> midi_data;
    for (unsigned char byte : img_data) {
        uint8_t midi1 = (byte >> 1) & 0x7F;
        uint8_t midi2 = (byte & 0x01) << 6;
        midi_data.push_back(midi1);
        midi_data.push_back(midi2);
    }

    write_midi_file(midi_output, midi_data);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: ./png2midi image.png output.mid\n";
        return 1;
    }
    try {
        read_png_binary(argv[1], argv[2]);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
