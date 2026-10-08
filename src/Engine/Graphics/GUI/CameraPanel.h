#pragma once

class DMCamera;

// Камера: положение, направление, строка для команды camera и параметра -Camera (копируется кнопкой), свойства камеры
class CameraPanel
{
public:
	void draw( DMCamera& camera, bool* open );
};
