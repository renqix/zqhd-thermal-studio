#pragma once
// 只向 UI 发布完整、预热完成的 32×32 摄氏度矩阵。
int thermalSensorBegin();
int thermalSensorRead(float *output);
int thermalSensorEmissivity();
bool thermalSensorSetEmissivity(int percent);
