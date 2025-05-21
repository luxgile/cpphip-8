#include "SDL3/SDL.h"
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <fstream>
#include <ios>
#include <iostream>
#include <print>
#include <ranges>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <variant>
#include <vector>

#define CHIP_ROM_ADDRESS 0x200
struct Rom {
  std::vector<u_int8_t> data;

  static std::expected<Rom, std::string> from_file(std::ifstream &file) {
    file.seekg(0, std::ios::end);
    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    Rom rom{};
    rom.data = std::vector<u_int8_t>(file_size);
    file.read(reinterpret_cast<char *>(rom.data.data()), file_size);
    /* for (u_int8_t byte : rom.data) { */
    /*   std::cout << static_cast<int>(byte) << "\n"; */
    /* } */
    std::cout << "loaded rom with size: " << file_size << "b\n";
    return rom;
  }
};

struct InstNotDef {
  u_int16_t address;
  u_int16_t inst;
};
struct PcOutOfBounds {};
using ChipError = std::variant<InstNotDef, PcOutOfBounds>;

#define CHIP_FONT_ADDRESS 0x100
const std::array<u_int8_t, 80> CHIP_FONT = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

#define SCREEN_WIDTH 64
#define SCREEN_HEIGHT 32
struct Chip8 {
  u_int8_t regs[16];
  u_int16_t reg_i;

  bool inputs[16];

  u_int16_t pc;
  std::vector<u_int16_t> last_inst;
  std::vector<u_int16_t> stack;

  bool screen[SCREEN_WIDTH * SCREEN_HEIGHT] = {0};

  u_int8_t delay_timer = 0;
  u_int8_t sound_timer = 0;

  std::vector<u_int8_t> memory;

  Chip8() {
    memory = std::vector<u_int8_t>(4096);

    for (int i = 0; i < 16; i++) {
      inputs[i] = 0;
    }

    // Copy the font to memory.
    std::copy(CHIP_FONT.begin(), CHIP_FONT.end(),
              memory.begin() + CHIP_FONT_ADDRESS);
  }

  void advance() { pc += 2; }

  void load_rom(Rom &rom) {
    std::copy(rom.data.begin(), rom.data.end(),
              memory.begin() + CHIP_ROM_ADDRESS);
    pc = CHIP_ROM_ADDRESS;
  }

  void print_screen() {
    std::cout << "\n";
    std::cout
        << "----------------------------------------------------------------";
    std::cout << "\n";
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
      if (i % SCREEN_WIDTH == 0)
        std::cout << "|\n|";

      std::cout << (screen[i] ? "#" : " ");
    }
    std::cout << "\n";
    std::cout
        << "----------------------------------------------------------------";
    std::cout << "\n";
  }

  void draw_screen(SDL_Renderer *renderer) {
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
      int x = i % SCREEN_WIDTH, y = i / SCREEN_WIDTH;
      if (screen[i])
        SDL_RenderPoint(renderer, x, y);
    }
  }

  std::expected<bool, ChipError> run_cycle() {
    if (pc >= memory.size())
      return std::unexpected(PcOutOfBounds{});

    u_int16_t inst = ((u_int16_t)memory[pc] << 8) | memory[pc + 1];
    /* std::cout << std::hex << inst << "\n"; */
    /* for (uint i = 0; i < 16; i++) { */
    /*   std::cout << i << " - " << inputs[i] << "|"; */
    /* } */
    /* std::cout << "\n"; */

    // Store the last 15 instructions for debug reasons.
    last_inst.push_back(inst);
    if (last_inst.size() >= 15)
      last_inst.erase(last_inst.begin());

    advance();

    switch (inst & 0xF000) {
    case 0x0000:
      if (inst == 0x00E0)
        clear_screen();
      else if (inst == 0x00EE) {
        pc = stack.back();
        stack.pop_back();
      } else if (inst == 0x0000) {
        return true; // Out of ROM bounds.
      } else {
        return std::unexpected(InstNotDef{pc, inst});
      }
      break;

    case 0x1000:
      pc = inst & 0x0FFF;
      break;

    case 0x2000: {
      int nnn = inst & 0x0FFF;
      stack.push_back(pc);
      pc = nnn;
      break;
    }

    case 0x3000: {
      u_int8_t vx = regs[(inst & 0x0F00) >> 8];
      int nn = inst & 0x00FF;
      if (vx == nn)
        advance();
      break;
    }

    case 0x4000: {
      u_int8_t vx = regs[(inst & 0x0F00) >> 8];
      int nn = inst & 0x00FF;
      if (vx != nn)
        advance();
      break;
    }

    case 0x5000: {
      u_int8_t vx = regs[(inst & 0x0F00) >> 8];
      u_int8_t vy = regs[(inst & 0x00F0) >> 4];
      if (vx == vy)
        advance();
      break;
    }

    case 0x6000: {
      u_int8_t &vx = regs[(inst & 0x0F00) >> 8];
      int nn = inst & 0x00FF;
      vx = nn;
      break;
    }

    case 0x7000: {
      u_int8_t &vx = regs[(inst & 0x0F00) >> 8];
      int nn = inst & 0x00FF;
      vx += nn;
      break;
    }

    case 0x8000: {
      u_int8_t &vf = regs[15];
      u_int8_t &vx = regs[(inst & 0x0F00) >> 8];
      u_int8_t vy = regs[(inst & 0x00F0) >> 4];

      if ((inst & 0x000F) == 0x0000)
        vx = vy;

      if ((inst & 0x000F) == 0x0001)
        vx = vx | vy;

      if ((inst & 0x000F) == 0x0002)
        vx = vx & vy;

      if ((inst & 0x000F) == 0x0003)
        vx = vx ^ vy;

      if ((inst & 0x000F) == 0x0004) {
        auto temp = vx > (255 - vy);
        vx += vy;
        vf = temp;
      }

      if ((inst & 0x000F) == 0x0005) {
        auto temp = vx < vy;
        vx -= vy;
        vf = !temp;
      }

      if ((inst & 0x000F) == 0x0006) {
        auto temp = vy & 0b00000001;
        vx = vy >> 1;
        vf = temp;
      }

      if ((inst & 0x000F) == 0x0007) {
        auto temp = vy < vx;
        vx = vy - vx;
        vf = !temp;
      }

      if ((inst & 0x000F) == 0x000E) {
        auto temp = (vy & 0b10000000) == 0b10000000;
        vx = vy << 1;
        vf = temp;
      }
      break;
    }

    case 0x9000: {
      u_int8_t vx = regs[(inst & 0x0F00) >> 8];
      u_int8_t vy = regs[(inst & 0x00F0) >> 4];
      if (vx != vy)
        advance();
      break;
    }

    case 0xA000: {
      reg_i = inst & 0x0FFF;
      break;
    }

    case 0xB000: {
      pc = inst & 0x0FFF;
      pc += regs[0];
      break;
    }

    case 0xC000: {
      u_int8_t &vx = regs[(inst & 0x0F00) >> 8];
      u_int16_t nn = inst & 0x0FF;
      u_int8_t random_value = rand() % 0xFF;
      vx = random_value & nn;
      break;
    }

    case 0xD000: {
      u_int8_t &vf = regs[15];
      u_int8_t vx = regs[(inst & 0x0F00) >> 8];
      u_int8_t vy = regs[(inst & 0x00F0) >> 4];
      u_int8_t n = inst & 0x000F;

      for (int i = 0; i < n; i++) {
        u_int8_t sprite = memory[reg_i + i];
        int y = (vy + i) % SCREEN_HEIGHT;

        for (int k = 0; k < 8; k++) {
          int x = (vx + k) % SCREEN_WIDTH;
          int px_index = y * SCREEN_WIDTH + x;
          bool &px = screen[px_index];
          bool pixel_val = ((sprite << k) & 0b10000000) == 0b10000000;

          if (pixel_val) {
            if (px)
              vf = 1;
            px ^= pixel_val;
          }
        }
      }

      break;
    }

    case 0xE000: {
      u_int8_t vx = regs[(inst & 0x0F00) >> 8];
      if ((inst & 0x00FF) == 0x009E && inputs[vx]) {
        advance();
      }

      if ((inst & 0x00FF) == 0x00A1 && !inputs[vx]) {
        advance();
      }
      break;
    }

    case 0xF000: {
      u_int8_t &vx = regs[(inst & 0x0F00) >> 8];
      if ((inst & 0x00FF) == 0x0007)
        vx = delay_timer;

      if ((inst & 0x0FF) == 0x000A) {
        bool any_pressed = false;

        for (uint i = 0; i <= 16; i++) {
          if (inputs[i]) {
            vx = i;
            any_pressed = true;
            break;
          }
        }

        if (!any_pressed) {
          pc -= 2;
        }
      }

      if ((inst & 0x00FF) == 0x0015)
        delay_timer = vx;

      if ((inst & 0x00FF) == 0x0018)
        sound_timer = vx;

      if ((inst & 0x00FF) == 0x001E)
        reg_i += vx;

      if ((inst & 0x0FF) == 0x0029) {
        reg_i = CHIP_FONT_ADDRESS +
                (vx * 5); // Each hex digit spans 5 elements in the font array.
      }

      if ((inst & 0x00FF) == 0x0033) {
        u_int8_t c = (vx / 100);
        u_int8_t d = (vx % 100) / 10;
        u_int8_t u = vx % 10;
        memory[reg_i] = c;
        memory[reg_i + 1] = d;
        memory[reg_i + 2] = u;
      }

      if ((inst & 0x00FF) == 0x0055) {
        int x = (inst & 0x0F00) >> 8;
        for (int i = 0; i <= x; i++) {
          memory[reg_i + i] = regs[i];
        }
        reg_i += x + 1;
      }

      if ((inst & 0x00FF) == 0x0065) {
        int x = (inst & 0x0F00) >> 8;
        for (int i = 0; i <= x; i++) {
          regs[i] = memory[reg_i + i];
        }
        reg_i += x + 1;
      }

      break;
    }

    default:
      return std::unexpected(InstNotDef{pc, inst});
    }

    return false;
  }

  void clear_screen() {
    for (bool &px : screen) {
      px = false;
    }
    std::cout << "screen cleared." << "\n";
  }
};

void handle_chip_error(ChipError &error) {
  if (auto ind = std::get_if<InstNotDef>(&error)) {
    std::cout << "inst not implemented " << std::hex << ind->inst
              << " at address " << std::hex << ind->address << "\n";
  } else if (auto _ = std::get_if<PcOutOfBounds>(&error)) {
    std::cout << "pc out of memory bounds. \n";
  } else {
    std::cout << "undefined error returned from chip8" << "\n";
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <filename>" << std::endl;
    return 1; // Indicate an error
  }

  std::ifstream rom_file(argv[1], std::ios::binary | std::ios::in);

  if (!rom_file.is_open()) {
    std::printf("no rom found. \n");
  }

  auto rom = Rom::from_file(rom_file).value();

  Chip8 chip{};
  chip.load_rom(rom);

  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Surface *surface;
  SDL_Event event;

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s",
                 SDL_GetError());
    return 3;
  }

  if (!SDL_CreateWindowAndRenderer("chip8", 800, 600, SDL_WINDOW_RESIZABLE,
                                   &window, &renderer)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Couldn't create window and renderer: %s", SDL_GetError());
    return 3;
  }

  /* SDL_SetRenderVSync(renderer, 1); */

  SDL_SetRenderLogicalPresentation(
      renderer, SCREEN_WIDTH, SCREEN_HEIGHT,
      SDL_RendererLogicalPresentation::SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

  SDL_AudioSpec spec = {
      .format = SDL_AUDIO_F32,
      .channels = 1,
      .freq = 44100,
  };

  SDL_AudioStream *audio = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);

  int num_samples = spec.freq * 0.2f;
  std::vector<float> audio_buffer(num_samples);
  for (int i = 0; i < num_samples; i++) {
    float t = (float)i / spec.freq;
    audio_buffer[i] = 0.5f * std::sin(1.0f * M_PI * 440.0f * t);
  }

  bool playing_sound = false;
  uint frame = 0;
  while (true) {
    SDL_PollEvent(&event);

    switch (event.type) {
    case SDL_EVENT_QUIT:
      return 0;
    case SDL_EVENT_KEY_DOWN:
      if (event.key.scancode == SDL_SCANCODE_X)
        chip.inputs[0x0] = true;
      if (event.key.scancode == SDL_SCANCODE_1)
        chip.inputs[0x1] = true;
      if (event.key.scancode == SDL_SCANCODE_2)
        chip.inputs[0x2] = true;
      if (event.key.scancode == SDL_SCANCODE_3)
        chip.inputs[0x3] = true;
      if (event.key.scancode == SDL_SCANCODE_Q)
        chip.inputs[0x4] = true;
      if (event.key.scancode == SDL_SCANCODE_W)
        chip.inputs[0x5] = true;
      if (event.key.scancode == SDL_SCANCODE_E)
        chip.inputs[0x6] = true;
      if (event.key.scancode == SDL_SCANCODE_A)
        chip.inputs[0x7] = true;
      if (event.key.scancode == SDL_SCANCODE_S)
        chip.inputs[0x8] = true;
      if (event.key.scancode == SDL_SCANCODE_D)
        chip.inputs[0x9] = true;
      if (event.key.scancode == SDL_SCANCODE_Z)
        chip.inputs[0xA] = true;
      if (event.key.scancode == SDL_SCANCODE_C)
        chip.inputs[0xB] = true;
      if (event.key.scancode == SDL_SCANCODE_4)
        chip.inputs[0xC] = true;
      if (event.key.scancode == SDL_SCANCODE_R)
        chip.inputs[0xD] = true;
      if (event.key.scancode == SDL_SCANCODE_F)
        chip.inputs[0xE] = true;
      if (event.key.scancode == SDL_SCANCODE_V)
        chip.inputs[0xF] = true;
      break;

    case SDL_EVENT_KEY_UP:
      if (event.key.scancode == SDL_SCANCODE_X)
        chip.inputs[0x0] = false;
      if (event.key.scancode == SDL_SCANCODE_1)
        chip.inputs[0x1] = false;
      if (event.key.scancode == SDL_SCANCODE_2)
        chip.inputs[0x2] = false;
      if (event.key.scancode == SDL_SCANCODE_3)
        chip.inputs[0x3] = false;
      if (event.key.scancode == SDL_SCANCODE_Q)
        chip.inputs[0x4] = false;
      if (event.key.scancode == SDL_SCANCODE_W)
        chip.inputs[0x5] = false;
      if (event.key.scancode == SDL_SCANCODE_E)
        chip.inputs[0x6] = false;
      if (event.key.scancode == SDL_SCANCODE_A)
        chip.inputs[0x7] = false;
      if (event.key.scancode == SDL_SCANCODE_S)
        chip.inputs[0x8] = false;
      if (event.key.scancode == SDL_SCANCODE_D)
        chip.inputs[0x9] = false;
      if (event.key.scancode == SDL_SCANCODE_Z)
        chip.inputs[0xA] = false;
      if (event.key.scancode == SDL_SCANCODE_C)
        chip.inputs[0xB] = false;
      if (event.key.scancode == SDL_SCANCODE_4)
        chip.inputs[0xC] = false;
      if (event.key.scancode == SDL_SCANCODE_R)
        chip.inputs[0xD] = false;
      if (event.key.scancode == SDL_SCANCODE_F)
        chip.inputs[0xE] = false;
      if (event.key.scancode == SDL_SCANCODE_V)
        chip.inputs[0xF] = false;
      break;

    default:
      break;
    }
    if (event.type == SDL_EVENT_QUIT) {
      break;
    }

    auto r = chip.run_cycle();

    if (!r) {
      handle_chip_error(r.error());
      break;
    }

    if (*r)
      break;

    if (chip.delay_timer > 0) {
      chip.delay_timer -= 1;
    }

    if (chip.sound_timer > 0) {
      if (!playing_sound) {
        SDL_ResumeAudioStreamDevice(audio);
        playing_sound = true;
      }

      chip.delay_timer -= 1;

      if (SDL_GetAudioStreamQueued(audio) < spec.freq * sizeof(float) / 10) {
        SDL_PutAudioStreamData(audio, audio_buffer.data(),
                               audio_buffer.size() * sizeof(float));
      }

    } else {
      if (playing_sound) {
        SDL_PauseAudioStreamDevice(audio);
        SDL_ClearAudioStream(audio);
        playing_sound = false;
      }
    }

    /* system("clear"); */
    /* chip.print_screen(); */

    SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0x00);
    SDL_RenderClear(renderer);

    chip.draw_screen(renderer);

    SDL_RenderPresent(renderer);

    /* usleep(1600); */
    frame += 1;
  }

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);

  SDL_Quit();

  return 0;
}
