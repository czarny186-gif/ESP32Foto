#include <Arduino.h>
#include <SD.h>
#include <LovyanGFX.hpp>
#include <algorithm>
#include <vector>

class HosyondDisplay : public lgfx::LGFX_Device {
  lgfx::Bus_SPI bus;
  lgfx::Panel_ST7796 panel;
  lgfx::Touch_XPT2046 touch;

public:
  HosyondDisplay() {
    auto b = bus.config();
    b.spi_host = SPI2_HOST;
    b.freq_write = 40000000;
    b.spi_mode = 0;
    b.dma_channel = SPI_DMA_CH_AUTO;
    b.freq_read = 16000000;
    b.spi_3wire = false;
    b.pin_sclk = 14;
    b.pin_mosi = 13;
    b.pin_miso = 12;
    b.pin_dc = 2;
    bus.config(b);
    panel.setBus(&bus);

    auto p = panel.config();
    p.pin_cs = 15;
    p.pin_rst = -1;
    p.memory_width = p.panel_width = 320;
    p.memory_height = p.panel_height = 480;
    p.bus_shared = true;
    p.offset_rotation = 0;
    p.readable = false;
    p.invert = false;
    p.rgb_order = false;
    panel.config(p);

    auto t = touch.config();
    t.spi_host = SPI2_HOST;
    t.freq = 1000000;
    t.pin_sclk = 14;
    t.pin_mosi = 13;
    t.pin_miso = 12;
    t.pin_cs = 33;
    t.pin_int = -1;
    t.bus_shared = true;
    t.x_min = 300;
    t.x_max = 3900;
    t.y_min = 200;
    t.y_max = 3900;
    t.offset_rotation = 0;
    touch.config(t);
    panel.setTouch(&touch);
    setPanel(&panel);
  }
};

HosyondDisplay tft;

struct Photo {
  String path;
  uint8_t turns = 0;
  explicit Photo(const char* filePath) : path(filePath) {}
};

struct Rect {
  int x, y, w, h;

  bool contains(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

enum class Action { Previous, RotatePhoto, RotateScreen, Reload, Next };

Rect previewRect();
Rect buttonRect(int index);
void drawIcon(Action action, int x, int y, uint32_t color);
void handleAction(Action action);

std::vector<Photo> photos;
size_t current = 0;
uint8_t screenRotation = 1;

constexpr int sdCs = 5;
constexpr int backlight = 27;
constexpr int margin = 10;
constexpr int header = 52;
constexpr int footer = 59;
constexpr uint32_t background = 0x0E1116;
constexpr uint32_t panelColor = 0x1A1F27;
constexpr uint32_t buttonColor = 0x262D38;
constexpr uint32_t borderColor = 0x353E4C;
constexpr uint32_t textColor = 0xE8ECF1;
constexpr uint32_t muted = 0xA8B3C2;
constexpr uint32_t accent = 0xF5A524;

Rect previewRect() {
  return {margin + 3, header + 3, tft.width() - 2 * (margin + 3),
          tft.height() - header - footer - 6};
}

Rect buttonRect(int index) {
  const int available = tft.width() - 2 * margin;
  const int left = margin + available * index / 5;
  const int right = margin + available * (index + 1) / 5;
  return {left, tft.height() - footer + 3, right - left - 5, footer - 12};
}

void drawIcon(Action action, int x, int y, uint32_t color) {
  switch (action) {
    case Action::Previous:
    case Action::Next: {
      const int direction = action == Action::Previous ? -1 : 1;
      tft.drawLine(x - 4 * direction, y - 5, x + 2 * direction, y, color);
      tft.drawLine(x + 2 * direction, y, x - 4 * direction, y + 5, color);
      break;
    }
    case Action::RotatePhoto:
      tft.drawRect(x - 6, y - 2, 10, 8, color);
      tft.drawLine(x - 3, y - 5, x + 6, y - 5, color);
      tft.drawLine(x + 6, y - 5, x + 6, y, color);
      tft.fillTriangle(x + 3, y - 1, x + 9, y - 1, x + 6, y + 2, color);
      break;
    case Action::RotateScreen:
      tft.drawRoundRect(x - 7, y - 5, 14, 10, 2, color);
      tft.drawLine(x - 3, y + 7, x + 3, y + 7, color);
      tft.drawLine(x, y + 5, x, y + 7, color);
      break;
    case Action::Reload:
      tft.drawCircle(x, y, 5, color);
      tft.fillTriangle(x + 2, y - 6, x + 8, y - 5, x + 5, y - 1, color);
      break;
  }
}

void drawFrame() {
  const int w = tft.width();
  const int h = tft.height();

  tft.startWrite();
  tft.fillRect(0, 0, w, header, background);
  tft.fillRect(0, h - footer, w, footer, background);
  tft.fillRoundRect(margin, header, w - 2 * margin, h - header - footer,
                    12, panelColor);
  tft.drawRoundRect(margin, header, w - 2 * margin, h - header - footer,
                    12, borderColor);

  tft.fillRoundRect(margin, 12, 4, 26, 2, accent);
  tft.setFont(&lgfx::fonts::FreeSansBold18pt7b);
  tft.setTextColor(textColor);
  tft.drawString("Galeria", margin + 12, 8);

  if (!photos.empty()) {
    const String counter = String(current + 1) + " / " + String(photos.size());
    tft.fillRoundRect(w - margin - 108, 12, 108, 27, 9, buttonColor);
    tft.setFont(&lgfx::fonts::FreeMonoBold9pt7b);
    tft.setTextColor(accent);
    tft.drawCentreString(counter, w - margin - 54, 18);
  }

  const char* labels[] = {"Wstecz", "Obroc", "Ekran", "Karta", "Dalej"};
  tft.setFont(&lgfx::fonts::FreeSans9pt7b);
  for (int i = 0; i < 5; ++i) {
    const Rect b = buttonRect(i);
    const bool next = i == static_cast<int>(Action::Next);
    const uint32_t fill = next ? accent : buttonColor;
    const uint32_t ink = next ? background : textColor;
    tft.fillRoundRect(b.x, b.y, b.w, b.h, 9, fill);
    if (!next) tft.drawRoundRect(b.x, b.y, b.w, b.h, 9, borderColor);
    drawIcon(static_cast<Action>(i), b.x + b.w / 2, b.y + 13, ink);
    tft.setTextColor(ink);
    tft.drawCentreString(labels[i], b.x + b.w / 2, b.y + 24);
  }
  tft.endWrite();
}

void drawMessage(const char* title, const char* detail) {
  const Rect p = previewRect();
  const int x = p.x + p.w / 2;
  const int y = p.y + p.h / 2;
  tft.setFont(&lgfx::fonts::FreeSansBold12pt7b);
  tft.setTextColor(textColor);
  tft.drawCentreString(title, x, y - 22);
  tft.setFont(&lgfx::fonts::FreeSans9pt7b);
  tft.setTextColor(muted);
  tft.drawCentreString(detail, x, y + 9);
}

void showPhoto() {
  const uint32_t start = millis();
  drawFrame();
  if (photos.empty()) {
    drawMessage("Brak zdjec", "Wloz karte i dotknij Karta");
    return;
  }

  const Photo& photo = photos[current];
  File file = SD.open(photo.path.c_str());
  Rect area = previewRect();
  int width = tft.width();
  int height = tft.height();
  for (uint8_t turn = 0; turn < photo.turns; ++turn) {
    area = {area.y, width - area.x - area.w, area.h, area.w};
    std::swap(width, height);
  }

  // Obracamy adresowanie GRAM tylko na czas zapisu zdjecia.
  // Przyciski pozostaja na miejscu, a dekoder nie potrzebuje bufora calej klatki.
  if (photo.turns) tft.setRotation((screenRotation + photo.turns) & 3);
  const bool drawn = file && tft.drawJpg(&file, area.x, area.y, area.w, area.h,
      0, 0, 0.0f, 0.0f, lgfx::datum_t::middle_center);
  if (photo.turns) tft.setRotation(screenRotation);
  file.close();

  if (!drawn) {
    drawFrame();
    drawMessage("Blad odczytu JPG", "Uzyj formatu JPEG baseline");
    return;
  }
  Serial.printf("JPEG: %lu ms\n", static_cast<unsigned long>(millis() - start));
}

void reloadCard() {
  photos.clear();
  current = 0;
  SD.end();
  if (!SD.begin(sdCs, SPI, 20000000) && !SD.begin(sdCs, SPI, 4000000)) {
    drawFrame();
    drawMessage("Brak karty microSD", "Sprawdz karte FAT32");
    return;
  }

  File directory = SD.open("/zdjecia");
  if (!directory || !directory.isDirectory()) directory = SD.open("/");
  if (!directory || !directory.isDirectory()) {
    drawFrame();
    drawMessage("Blad katalogu", "Sprawdz pliki na karcie");
    return;
  }

  while (photos.size() < 500) {
    File file = directory.openNextFile();
    if (!file) break;
    if (!file.isDirectory()) {
      String name = file.name();
      name.toLowerCase();
      if (name.endsWith(".jpg") || name.endsWith(".jpeg")) {
        photos.emplace_back(file.path());
      }
    }
  }
  directory.close();

  std::sort(photos.begin(), photos.end(), [](const Photo& a, const Photo& b) {
    return a.path < b.path;
  });
  showPhoto();
}

void handleAction(Action action) {
  switch (action) {
    case Action::RotateScreen:
      screenRotation = (screenRotation + 1) & 3;
      // LovyanGFX przelicza getTouch() wedlug tej samej orientacji panelu.
      tft.setRotation(screenRotation);
      tft.fillScreen(background);
      break;
    case Action::Reload:
      reloadCard();
      return;
    case Action::RotatePhoto:
      if (photos.empty()) return;
      photos[current].turns = (photos[current].turns + 1) & 3;
      break;
    case Action::Previous:
      if (photos.empty()) return;
      current = (current + photos.size() - 1) % photos.size();
      break;
    case Action::Next:
      if (photos.empty()) return;
      current = (current + 1) % photos.size();
      break;
  }
  showPhoto();
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(screenRotation);
  tft.fillScreen(background);
  pinMode(backlight, OUTPUT);
  digitalWrite(backlight, HIGH);
  SPI.begin(18, 19, 23, sdCs);
  reloadCard();
}

void loop() {
  static bool wasPressed = false;
  uint16_t x, y;
  const bool pressed = tft.getTouch(&x, &y);
  const bool tapped = pressed && !wasPressed;
  wasPressed = pressed;
  if (tapped) {
    for (int i = 0; i < 5; ++i) {
      if (buttonRect(i).contains(x, y)) {
        handleAction(static_cast<Action>(i));
        break;
      }
    }
  }
  delay(25);
}
