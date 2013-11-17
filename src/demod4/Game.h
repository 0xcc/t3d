#include <windows.h>
#include <stdio.h>

namespace bspeditor { class Gadgets; class EditTask; }

namespace t3d {

class BmpImg;
class ZBuffer;
class RenderList;
class RenderObject;

class Game
{
public:
	static const int WINDOW_WIDTH = 800;
	static const int WINDOW_HEIGHT = 600;
	static const int WINDOW_BPP = 32; // bitdepth of window (8,16,24 etc.)
	// note: if windowed and not
	// fullscreen then bitdepth must
	// be same as system bitdepth
	// also if 8-bit the a pallete
	// is created and attached
	static const int WINDOWED_APP = 1; // 0 not windowed, 1 windowed

public:
	Game(HWND handle, HINSTANCE instance);
	~Game();

	void Init();
	void Shutdown();
	void Step();

private:
	static const int AMBIENT_LIGHT_INDEX	= 0; // ambient light index
	static const int INFINITE_LIGHT_INDEX	= 1; // infinite light index
	static const int POINT_LIGHT_INDEX		= 2; // point light index
	static const int POINT_LIGHT2_INDEX		= 4; // point light index

private:
	HWND main_window_handle; // save the window handle
	HINSTANCE main_instance; // save the instance
	char buffer[256];        // used to print text

	bspeditor::EditTask* task;

	bspeditor::Gadgets* gadgets;

	BmpImg* background_bmp;   // holds the background

	RenderList* list;

	ZBuffer* zbuffer;

}; // Game

}