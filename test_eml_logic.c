#include "eml_calculator.h"
#include <stdio.h>

void test_eml(double temp, double hum, const char *label) {
  env_moisture_level_t level = eml_classify_moisture(temp, hum);
  const char *str = eml_get_level_str(level);
  printf("[%s] T=%.1f, H=%.1f -> Level=%d (%s)\n", label, temp, hum, level,
         str);
}

int main() {
  printf("=== EML Logic & String Verification ===\n");
  test_eml(22.0, 50.0, "Neutral");
  test_eml(20.0, 20.0, "Very Dry");
  test_eml(28.0, 48.0, "Hot & Moist Check");
  test_eml(15.0, 50.0, "Cold & Dry Check");
  return 0;
}
