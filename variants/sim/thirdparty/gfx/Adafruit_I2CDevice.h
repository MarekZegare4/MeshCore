#pragma once
// Adafruit_GFX.h #includes this unconditionally, but nothing in
// Adafruit_GFX.h/.cpp actually references any symbol from it (checked by
// grep) -- it's real hardware I2C plumbing Adafruit_GFX itself never needs,
// only its device-specific subclasses (Adafruit_SSD1306 etc., which this
// sim doesn't compile at all -- see variants/sim/SimDisplayDriver.h's own
// canvas backend instead). An empty stub is enough to satisfy the #include.
