#pragma once

#include "Engine\Console\ConsoleCommands.h"

namespace GS
{

class Scene;

// Команды подсистем сцены в консоли и удалённом управлении (docs/remote.md): terrain — кисть слоёв и масок (paint) и выгрузка
// русел (hydrology), water — выгрузка расхода и состояния воды. Разбор аргументов — здесь, дело — у подсистем
// (TerrainPaintTool, TerrainPipeline, WaterSimulation); scene живёт дольше консоли
void registerSceneCommands( ConsoleCommands& console, Scene& scene );

}
