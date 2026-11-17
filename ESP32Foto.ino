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
std::vector<String> photos;
size_t current = 0;
constexpr int sdCs = 5;
constexpr int backlight = 27;
constexpr int toolbar = 48;
constexpr int margin = 8;
constexpr int header = 28;
constexpr int rotation = 1; // Ustaw 3, jesli caly ekran jest obrocony o 180 stopni.
constexpr uint32_t background = 0x0E1116;
constexpr uint32_t panelColor = 0x1A1F27;
constexpr uint32_t buttonColor = 0x262D38;
constexpr uint32_t borderColor = 0x353E4C;
constexpr uint32_t textColor = 0xE8ECF1;
constexpr uint32_t accentColor = 0xF5A524;

void drawToolbar() {
  const int y = tft.height() - toolbar;
  const int sectionWidth = tft.width() / 3;
  const char* labels[] = {"< Wstecz", "Odswiez", "Dalej >"};
  tft.fillRect(0, y, tft.width(), toolbar, background);
  tft.setFont(&lgfx::fonts::Font2);
  for (int i = 0; i < 3; ++i) {
    const int x = sectionWidth * i + margin;
    const int width = sectionWidth - 2 * margin;
    const uint32_t fill = i == 2 ? accentColor : buttonColor;
    tft.fillRoundRect(x, y + 4, width, 36, 10, fill);
    tft.drawRoundRect(x, y + 4, width, 36, 10,
                      i == 2 ? accentColor : borderColor);
    tft.setTextColor(i == 2 ? background : textColor, fill);
    tft.drawCentreString(labels[i], x + width / 2, y + 13);
  }
}

void startFrame() {
  tft.startWrite();
  tft.fillRect(0, 0, tft.width(), header, background);
  tft.fillRoundRect(margin, header, tft.width() - 2 * margin,
                    tft.height() - header - toolbar, 12, panelColor);
  tft.setFont(&lgfx::fonts::Font2);
  tft.setTextColor(textColor, background);
  tft.drawString("Zdjecia", margin, 5);
  if (!photos.empty()) {
    const String counter = String(current + 1) + " / " + String(photos.size());
    tft.drawRightString(counter, tft.width() - margin, 5);
  }
  drawToolbar();
  tft.endWrite();
}

void drawMessage(const char* message) {
  tft.setTextColor(textColor, panelColor);
  tft.drawCentreString(message, tft.width() / 2,
                       (header + tft.height() - toolbar) / 2 - 8);
}

void showMessage(const char* message) {
  startFrame();
  drawMessage(message);
}

void showPhoto() {
  const uint32_t start = millis();
  startFrame();
  File file = SD.open(photos[current].c_str());
  const bool drawn = file && tft.drawJpg(&file, margin + 4, header + 4,
      tft.width() - 2 * margin - 8, tft.height() - header - toolbar - 8,
      0, 0, 0.0f, 0.0f, lgfx::datum_t::middle_center);
  file.close();
  if (!drawn) {
    showMessage("Blad odczytu. Zapisz plik jako JPEG baseline.");
    return;
  }
  Serial.printf("JPEG: %lu ms\n", static_cast<unsigned long>(millis() - start));
}

void reloadCard() {
  photos.clear();
  current = 0;
  SD.end();
  if (!SD.begin(sdCs, SPI, 20000000) && !SD.begin(sdCs, SPI, 4000000)) {
    showMessage("Sprawdz karte microSD (FAT32).");
    return;
  }

  File directory = SD.open("/zdjecia");
  if (!directory || !directory.isDirectory()) {
    directory = SD.open("/");
  }
  if (!directory || !directory.isDirectory()) {
    showMessage("Nie mozna otworzyc folderu na karcie.");
    return;
  }

  while (photos.size() < 500) {
    File file = directory.openNextFile();
    if (!file) break;
    if (!file.isDirectory()) {
      String extension = file.name();
      extension.toLowerCase();
      if (extension.endsWith(".jpg") || extension.endsWith(".jpeg")) {
        photos.emplace_back(file.path());
      }
    }
  }
  directory.close();

  if (photos.empty()) {
    showMessage("Brak plikow JPG lub JPEG.");
    return;
  }

  std::sort(photos.begin(), photos.end());
  showPhoto();
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(rotation);
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

  if (tapped && y >= tft.height() - toolbar) {
    const int section = 3 * x / tft.width();
    if (section == 1) {
      reloadCard();
    } else if (!photos.empty()) {
      current = section == 0
                  ? (current + photos.size() - 1) % photos.size()
                  : (current + 1) % photos.size();
      showPhoto();
    }
  }
  delay(25);
}
