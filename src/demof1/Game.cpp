#include "Game.h"

#include "Modules.h"
#include "Log.h"
#include "Graphics.h"
#include "Input.h"
#include "Sound.h"
#include "Music.h"
#include "Timer.h"
#include "defines.h"
#include "Camera.h"
#include "PrimitiveDraw.h"
#include "Light.h"
#include "RenderList.h"
#include "RenderObject.h"
#include "BitmapFont.h"
#include "BmpFile.h"
#include "BOB.h"
#include "BmpImg.h"
#include "ZBuffer.h"
#include "MD2.h"
 
namespace t3d {

#define PITCH_RETURN_RATE (.33) // how fast the pitch straightens out
#define PITCH_CHANGE_RATE (.02) // the rate that pitch changes relative to height
#define CAM_HEIGHT_SCALER (.3)  // percentage cam height changes relative to height
#define VELOCITY_SCALER (.025)  // rate velocity changes with height
#define CAM_DECEL       (.25)   // deceleration/friction

#define INDEX_RED_LIGHT_INDEX       0
#define INDEX_GREEN_LIGHT_INDEX     1
#define INDEX_BLUE_LIGHT_INDEX      2
#define INDEX_YELLOW_LIGHT_INDEX    3
#define INDEX_WHITE_LIGHT_INDEX     4

// filenames of objects to load to represent light sources
char *object_light_filenames[Game::NUM_LIGHT_OBJECTS] = {
                                            "../../assets/chap15/cube_constant_red_01.cob",
                                            "../../assets/chap15/cube_constant_green_01.cob",  
                                            "../../assets/chap15/cube_constant_blue_01.cob",
                                            "../../assets/chap15/cube_constant_yellow_01.cob",
                                            "../../assets/chap15/cube_constant_white_01.cob",
                                            };
int curr_light_object  = 0;

Game::Game(HWND handle, HINSTANCE instance)
	: _cam(NULL)
	, main_window_handle(handle)
	, main_instance(instance)
{
	obj_terrain = new RenderObject;
	for (int index_obj=0; index_obj < NUM_LIGHT_OBJECTS; index_obj++)
		obj_light_array[index_obj] = new RenderObject;
	obj_model = new RenderObject;
	_list = new RenderList;
	_zbuffer = new ZBuffer;

	obj_md2 = new MD2Container;

	cockpit = new BOB;

	wind_sound_id = -1;

	// physical model defines play with these to change the feel of the vehicle
	gravity    = -.40;    // general gravity
	vel_y      = 0;       // the y velocity of camera/jeep
	cam_speed  = 0;       // speed of the camera/jeep
	sea_level  = 50;      // sea level of the simulation
	gclearance = 150;      // clearance from the camera to the ground
	neutral_pitch = 10;   // the neutral pitch of the camera
}

Game::~Game()
{
	delete cockpit;
	delete obj_md2;
	delete _zbuffer;
	delete _list;
	delete obj_model;
	for (int index_obj=0; index_obj < NUM_LIGHT_OBJECTS; index_obj++)
		delete obj_light_array[index_obj];
	delete obj_terrain;
}

void Game::Init()
{
	Modules::Init();

	Modules::GetLog().Init();

	Modules::GetGraphics().Init(main_window_handle,
		WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_BPP, WINDOWED_APP);

	Modules::GetInput().Init(main_instance);
	Modules::GetInput().InitKeyboard(main_window_handle);

	Modules::GetSound().Init(main_window_handle);
//	Modules::GetMusic().Init(main_window_handle);

	// hide the mouse
	if (!WINDOWED_APP)
		ShowCursor(FALSE);

	// seed random number generator
	srand(Modules::GetTimer().Start_Clock());

// 	// initialize math engine
// 	Build_Sin_Cos_Tables();

	// initialize the camera with 90 FOV, normalized coordinates
	_cam = new Camera(CAM_MODEL_EULER,		// the euler model
					  vec4(0,500,-400,1),	// initial camera position
					  vec4(0,0,0,1),		// initial camera angles
					  vec4(0,0,0,1),		// no target
					  10.0,					// near and far clipping planes
					  12000.0,
					  90.0,					// field of view in degrees
					  WINDOW_WIDTH,			// size of final screen viewport
					  WINDOW_HEIGHT);

	vec4 terrain_pos(0,0,0,0);
	obj_terrain->GenerateTerrain(TERRAIN_WIDTH,				// width in world coords on x-axis
								 TERRAIN_HEIGHT,			// height (length) in world coords on z-axis
								 TERRAIN_SCALE,				// vertical scale of terrain
								"../../assets/chap15/height_grass_40_40_01.bmp",  // filename of height bitmap encoded in 256 colors
								"../../assets/chap15/stone256_256_01.bmp",   // filename of texture map
								_RGB32BIT(255,255,255,255), // color of terrain if no texture        
								&terrain_pos,				// initial position
								NULL,						// initial rotations
								POLY_ATTR_RGB24 | 
								POLY_ATTR_SHADE_MODE_FLAT | /* POLY_ATTR_SHADE_MODE_GOURAUD */
								POLY_ATTR_SHADE_MODE_GOURAUD |
								POLY_ATTR_SHADE_MODE_TEXTURE);

	vec4 vscale(1.0,1.0,1.0,1), 
         vpos(0,0,150,1), 
         vrot(0,0,0,1);

	// set a scaling vector
	vscale.Assign(20, 20, 20);

	// load all the light objects in
	for (int index_obj=0; index_obj < NUM_LIGHT_OBJECTS; index_obj++)
	{
		obj_light_array[index_obj]->LoadCOB(object_light_filenames[index_obj],  
											&vscale, &vpos, &vrot, VERTEX_FLAGS_INVERT_WINDING_ORDER 
											| VERTEX_FLAGS_TRANSFORM_LOCAL 
											| VERTEX_FLAGS_TRANSFORM_LOCAL_WORLD
											,0 );
	} // end for index

	// set current object
	curr_light_object = 0;
	obj_light    = obj_light_array[curr_light_object];

	// set up lights
	LightsMgr& lights = Modules::GetGraphics().GetLights();
	lights.Reset();

	// create some working colors
	Color white(255,255,255,0),
		  gray(100,100,100,0),
		  black(0,0,0,0),
		  red(255,0,0,0),
		  green(0,255,0,0),
		  blue(0,0,255,0),
		  orange(255,128,0,0),
		  yellow(255,255,0,0);

	// ambient light
	lights.Init(AMBIENT_LIGHT_INDEX,   
				LIGHT_STATE_ON,      // turn the light on
				LIGHT_ATTR_AMBIENT,  // ambient light type
				gray, black, black,    // color for ambient term only
				NULL, NULL,            // no need for pos or dir
				0,0,0,                 // no need for attenuation
				0,0,0);                // spotlight info NA

	vec4 dlight_dir(-1,1,-1,1);

	// directional light
	lights.Init(INFINITE_LIGHT_INDEX,  
				LIGHT_STATE_ON,      // turn the light on
				LIGHT_ATTR_INFINITE, // infinite light type
				black, gray, black,    // color for diffuse term only
				NULL, &dlight_dir,     // need direction only
				0,0,0,                 // no need for attenuation
				0,0,0);                // spotlight info NA

	vec4 plight_pos(0,500,0,1);

	// point light
	lights.Init(POINT_LIGHT_INDEX,
				LIGHT_STATE_ON,      // turn the light on
				LIGHT_ATTR_POINT,    // pointlight type
				black, orange, black,   // color for diffuse term only
				&plight_pos, NULL,     // need pos only
				0,.001,0,              // linear attenuation only
				0,0,1);                // spotlight info NA

	// spot light2
	lights.Init(POINT_LIGHT2_INDEX,
				LIGHT_STATE_ON,         // turn the light on
				LIGHT_ATTR_POINT,  // spot light type 2
				black, yellow, black,      // color for diffuse term only
				&plight_pos, NULL, // need pos only
				0,.002,0,                 // linear attenuation only
				0,0,1);    


// 	// create lookup for lighting engine
// 	RGB_16_8_IndexedRGB_Table_Builder(DD_PIXEL_FORMAT565,  // format we want to build table for
// 		palette,             // source palette
// 		rgblookup);          // lookup table

	// create the z buffer
	_zbuffer->Create(WINDOW_WIDTH,
		WINDOW_HEIGHT,
		ZBUFFER_ATTR_32BIT);

// 	// build alpha lookup table
// 	RGB_Alpha_Table_Builder(NUM_ALPHA_LEVELS, rgb_alpha_table);

	// load background sounds
	Sound& sound = Modules::GetSound();
	wind_sound_id = sound.LoadWAV("../../assets/chap15/STATIONTHROB.WAV");

	// start the sounds
// 	sound.Play(wind_sound_id, DSBPLAY_LOOPING);
// 	sound.Play(car_sound_id, DSBPLAY_LOOPING);
// 
// 	sound.SetVolume(car_sound_id, 70);
// 	sound.SetVolume(wind_sound_id, 100);

	// load background image that scrolls 
	BmpFile* bitmap = new BmpFile("../../assets/chap15/sunset800_600_03.bmp");
	background_bmp = new BmpImg(0,0,800,600,32);
	background_bmp->LoadImage32(*bitmap,0,0,BITMAP_EXTRACT_MODE_ABS);
	delete bitmap;

	static vec4 vs(4,4,4,1);
	static vec4 vp(0,0,0,1);

	// load the md2 object
// 	obj_md2->Load("../../assets/chap15/md2/q2mdl-tekkblade/tris.md2", //  "D:/Games/quakeII/baseq2/players/male/tris.md2",    // the filename of the .MD2 model
// 					&vs, 
// 					&vp,
// 					NULL,          
// 					"../../assets/chap15/md2/q2mdl-tekkblade/blade_black.bmp", //"D:/Games/quakeII/baseq2/players/male/claymore.bmp",   // the texture filename for the model
// 					POLY_ATTR_RGB16 | POLY_ATTR_SHADE_MODE_FLAT | POLY_ATTR_SHADE_MODE_TEXTURE,
// 					_RGB32BIT(255,255,255,255),
// 					VERTEX_FLAGS_SWAP_YZ);                          // control ordering etc.

	obj_md2->Load("../../assets/chap15/md2/q2mdl-wham/tris.md2", //  "D:/Games/quakeII/baseq2/players/male/tris.md2",    // the filename of the .MD2 model
					&vs, 
					&vp,
					NULL,          
					"../../assets/chap15/md2/q2mdl-wham/tundra.bmp", //"D:/Games/quakeII/baseq2/players/male/claymore.bmp",   // the texture filename for the model
					POLY_ATTR_RGB16 | POLY_ATTR_SHADE_MODE_FLAT | POLY_ATTR_SHADE_MODE_TEXTURE,
					_RGB32BIT(255,255,255,255),
					VERTEX_FLAGS_SWAP_YZ);                          // control ordering etc.

	// prepare OBJECT4DV2 for md2
	obj_md2->PrepareObject(obj_model);

	// set the animation
	obj_md2->SetAnimation(MD2_ANIM_STATE_STANDING_IDLE, MD2_ANIM_LOOP);

#if 0
	// play with these for more speed :)
	// set single precission
	_control87( _PC_24, _MCW_PC );

	// set to flush mode
	_control87( _DN_FLUSH, _MCW_DN );

	// set rounding mode
	_control87( _RC_NEAR, _MCW_RC );

#endif
}

void Game::Shutdown()
{
	delete _cam;

	Modules::GetSound().StopAllSounds();
	Modules::GetSound().DeleteAllSounds();
	Modules::GetSound().Shutdown();

// 	Modules::GetMusic().DeleteAllMIDI();
// 	Modules::GetMusic().Shutdown();

	Modules::GetInput().ReleaseKeyboard();
	Modules::GetInput().Shutdown();

	Modules::GetGraphics().Shutdown();

	Modules::GetLog().Shutdown();
}

void Game::Step()
{
	// todo zz
	int debug_polys_rendered_per_frame = 0;
	int debug_polys_lit_per_frame = 0;

	static mat4 mrot;   // general rotation matrix

	// state variables for different rendering modes and help
	static bool wireframe_mode = false;
	static bool backface_mode  = true;
	static bool lighting_mode  = true;
	static bool help_mode      = true;
	static bool zsort_mode     = false;
	static bool x_clip_mode    = true;
	static bool y_clip_mode    = true;
	static bool z_clip_mode    = true;

	static float hl = 300, // artificial light height
             ks = 1.25; // generic scaling factor to make things look good

	char work_string[256]; // temp string

	Graphics& graphics = Modules::GetGraphics();

	// start the timing clock
	Modules::GetTimer().Start_Clock();

	// clear the drawing surface 
//	graphics.FillSurface(graphics.GetBackSurface(), 0);

	// draw the sky
	DrawRectangle(0,0, WINDOW_WIDTH, WINDOW_HEIGHT, graphics.GetColor(255,120,255), graphics.GetBackSurface());

	// read keyboard and other devices here
	Modules::GetInput().ReadKeyboard();

	// game logic here...

	// reset the render _list
	_list->Reset();

	// modes and lights

	// wireframe mode
	if (Modules::GetInput().KeyboardState()[DIK_W])
	{
		// toggle wireframe mode
		wireframe_mode = !wireframe_mode;
		Modules::GetTimer().Wait_Clock(100); // wait, so keyboard doesn't bounce
	} // end if

	// backface removal
	if (Modules::GetInput().KeyboardState()[DIK_B])
	{
		// toggle backface removal
		backface_mode = !backface_mode;
		Modules::GetTimer().Wait_Clock(100); // wait, so keyboard doesn't bounce
	} // end if

	// lighting
	if (Modules::GetInput().KeyboardState()[DIK_L])
	{
		// toggle lighting engine completely
		lighting_mode = !lighting_mode;
		Modules::GetTimer().Wait_Clock(100); // wait, so keyboard doesn't bounce
	} // end if

	LightsMgr& lights = Modules::GetGraphics().GetLights();

	// toggle ambient light
	if (Modules::GetInput().KeyboardState()[DIK_A])
	{
		// toggle ambient light
		if (lights[AMBIENT_LIGHT_INDEX].state == LIGHT_STATE_ON)
			lights[AMBIENT_LIGHT_INDEX].state = LIGHT_STATE_OFF;
		else
			lights[AMBIENT_LIGHT_INDEX].state = LIGHT_STATE_ON;

		Modules::GetTimer().Wait_Clock(100); // wait, so keyboard doesn't bounce
	} // end if

	// toggle infinite light
	if (Modules::GetInput().KeyboardState()[DIK_I])
	{
		// toggle ambient light
		if (lights[INFINITE_LIGHT_INDEX].state == LIGHT_STATE_ON)
			lights[INFINITE_LIGHT_INDEX].state = LIGHT_STATE_OFF;
		else
			lights[INFINITE_LIGHT_INDEX].state = LIGHT_STATE_ON;

		Modules::GetTimer().Wait_Clock(100); // wait, so keyboard doesn't bounce
	} // end if

	// toggle point light
	if (Modules::GetInput().KeyboardState()[DIK_P])
	{
		// toggle point light
		if (lights[POINT_LIGHT_INDEX].state == LIGHT_STATE_ON)
			lights[POINT_LIGHT_INDEX].state = LIGHT_STATE_OFF;
		else
			lights[POINT_LIGHT_INDEX].state = LIGHT_STATE_ON;

		// toggle point light
		if (lights[POINT_LIGHT2_INDEX].state == LIGHT_STATE_ON)
			lights[POINT_LIGHT2_INDEX].state = LIGHT_STATE_OFF;
		else
			lights[POINT_LIGHT2_INDEX].state = LIGHT_STATE_ON;

		Modules::GetTimer().Wait_Clock(100); // wait, so keyboard doesn't bounce
	} // end if

	// help menu
	if (Modules::GetInput().KeyboardState()[DIK_H])
	{
		// toggle help menu 
		help_mode = !help_mode;
		Modules::GetTimer().Wait_Clock(100); // wait, so keyboard doesn't bounce
	} // end if

	// z-sorting
	if (Modules::GetInput().KeyboardState()[DIK_Z])
	{
		// toggle z sorting
		zsort_mode = !zsort_mode;
		Modules::GetTimer().Wait_Clock(100); // wait, so keyboard doesn't bounce
	} // end if

	// next animation
	if (Modules::GetInput().KeyboardState()[DIK_2])
	{
		if (++obj_md2->_anim_state >= NUM_MD2_ANIMATIONS)
			obj_md2->_anim_state = 0;  

		obj_md2->SetAnimation(obj_md2->_anim_state, MD2_ANIM_SINGLE_SHOT);

		Modules::GetTimer().Wait_Clock(100); // wait, so keyboard doesn't bounce
	} // end if


	// previous animation
	if (Modules::GetInput().KeyboardState()[DIK_1])
	{
		if (--obj_md2->_anim_state < 0)
			obj_md2->_anim_state = NUM_MD2_ANIMATIONS-1;  

		obj_md2->SetAnimation(obj_md2->_anim_state, MD2_ANIM_SINGLE_SHOT);

		Modules::GetTimer().Wait_Clock(100); // wait, so keyboard doesn't bounce
	} // end if

	// replay animation
	if (Modules::GetInput().KeyboardState()[DIK_3])
	{
		obj_md2->SetAnimation(obj_md2->_anim_state, MD2_ANIM_SINGLE_SHOT);
		Modules::GetTimer().Wait_Clock(100); // wait, so keyboard doesn't bounce
	} // end if


	// replay animation
	if (Modules::GetInput().KeyboardState()[DIK_4])
	{
		obj_md2->SetAnimation(obj_md2->_anim_state, MD2_ANIM_LOOP);
		Modules::GetTimer().Wait_Clock(100); // wait, so keyboard doesn't bounce
	} // end if

	// object and camera movement

	// rotate around y axis or yaw
	if (Modules::GetInput().KeyboardState()[DIK_RIGHT])
	{
		_cam->DirRef().y+=5;
		// scroll the background
		background_bmp->Scroll(-10);
	} // end if

	if (Modules::GetInput().KeyboardState()[DIK_LEFT])
	{
		_cam->DirRef().y-=5;
		// scroll the background
		background_bmp->Scroll(10);
	} // end if

	if (Modules::GetInput().KeyboardState()[DIK_UP])
	{
		// move forward
		if ( (cam_speed+=1) > MAX_SPEED) cam_speed = MAX_SPEED;
	} // end if

	if (Modules::GetInput().KeyboardState()[DIK_DOWN])
	{
		// move backward
		if ((cam_speed-=1) < -MAX_SPEED) cam_speed = -MAX_SPEED;
	} // end if

	// scroll sky slowly
	background_bmp->Scroll(-1);

	// motion section /////////////////////////////////////////////////////////

	// terrain following, simply find the current cell we are over and then
	// index into the vertex _list and find the 4 vertices that make up the 
	// quad cell we are hovering over and then average the values, and based
	// on the current height and the height of the terrain push the player upward

	// the terrain generates and stores some results to help with terrain following
	//GetIVar1() = columns;
	//GetIVar2() = rows;
	//fvar1 = col_vstep;
	//fvar2 = row_vstep;

	int cell_x = (_cam->Pos().x  + TERRAIN_WIDTH/2) / obj_terrain->GetFVar1();
	int cell_y = (_cam->Pos().z  + TERRAIN_HEIGHT/2) / obj_terrain->GetFVar1();

	static float terrain_height, delta;

	// test if we are on terrain
	if ( (cell_x >=0) && (cell_x < obj_terrain->GetIVar1()) && (cell_y >=0) && (cell_y < obj_terrain->GetIVar2()) )
	{
		// compute vertex indices into vertex _list of the current quad
		int v0 = cell_x + cell_y*obj_terrain->GetIVar2();
		int v1 = v0 + 1;
		int v2 = v1 + obj_terrain->GetIVar2();
		int v3 = v0 + obj_terrain->GetIVar2();   

		// now simply index into table 
		terrain_height = 0.25 * (obj_terrain->GetTransVertex(v0).y + obj_terrain->GetTransVertex(v1).y +
			obj_terrain->GetTransVertex(v2).y + obj_terrain->GetTransVertex(v3).y);

		// compute height difference
		delta = terrain_height - (_cam->Pos().y - gclearance);

		// test for penetration
		if (delta > 0)
		{
			// apply force immediately to camera (this will give it a springy feel)
			vel_y+=(delta * (VELOCITY_SCALER));

			// test for pentration, if so move up immediately so we don't penetrate geometry
			_cam->PosRef().y += (delta*CAM_HEIGHT_SCALER);

			// now this is more of a hack than the physics model :) let move the front
			// up and down a bit based on the forward velocity and the gradient of the 
			// hill
			_cam->DirRef().x -= (delta*PITCH_CHANGE_RATE);

		} // end if

	} // end if

	// decelerate camera
	if (cam_speed > (CAM_DECEL) ) cam_speed-=CAM_DECEL;
	else if (cam_speed < (-CAM_DECEL) ) cam_speed+=CAM_DECEL;
	else cam_speed = 0;

	// force camera to seek a stable orientation
	if (_cam->Dir().x > (neutral_pitch+PITCH_RETURN_RATE)) _cam->DirRef().x -= (PITCH_RETURN_RATE);
	else if (_cam->Dir().x < (neutral_pitch-PITCH_RETURN_RATE)) _cam->DirRef().x += (PITCH_RETURN_RATE);
	else _cam->DirRef().x = neutral_pitch;

	// apply gravity
	vel_y+=gravity;

	// test for absolute sea level and push upward..
	if (_cam->Pos().y < sea_level)
	{ 
		vel_y = 0; 
		_cam->PosRef().y = sea_level;
	} // end if

	// move camera
	_cam->PosRef().x += cam_speed*sin(DEG_TO_RAD(_cam->Dir().y));
	_cam->PosRef().z += cam_speed*cos(DEG_TO_RAD(_cam->Dir().y));
	_cam->PosRef().y += vel_y;

	static float plight_ang = 0; // angles for light motion

	// move point light source in ellipse around game world
	lights[POINT_LIGHT_INDEX].pos.x = 500*cos(DEG_TO_RAD(plight_ang));
//	lights[POINT_LIGHT_INDEX].pos.y = 200;
	lights[POINT_LIGHT_INDEX].pos.z = 500*sin(DEG_TO_RAD(plight_ang));

	// move spot light source in ellipse around game world
	lights[POINT_LIGHT2_INDEX].pos.x = 200*cos(DEG_TO_RAD(-2*plight_ang));
//	lights[POINT_LIGHT2_INDEX].pos.y = 400;
	lights[POINT_LIGHT2_INDEX].pos.z = 200*sin(DEG_TO_RAD(-2*plight_ang));

	if ((plight_ang+=1) > 360)
		plight_ang = 0;

	// generate camera matrix
	_cam->BuildMatrixEuler(CAM_ROT_SEQ_ZYX);

	//////////////////////////////////////////////////////////////////////////
	// constant shaded water

	// reset the object (this only matters for backface and object removal)
	obj_terrain->Reset();

 	// use these to rotate objects
 	static float x_ang = 0, y_ang = 0, z_ang = 0;
 
 	// generate rotation matrix around y axis
 	mrot = mat4::RotateX(x_ang) * mat4::RotateY(y_ang) * mat4::RotateZ(z_ang);
 
 	// rotate the local coords of the object
 	obj_terrain->Transform(mrot, TRANSFORM_LOCAL_TO_TRANS,true);

	// perform world transform
	obj_terrain->ModelToWorld(TRANSFORM_TRANS_ONLY);

	// insert the object into render _list
	_list->Insert(*obj_terrain, false);

	//////////////////////////////////////////////////////////////////////////

	int v0, v1, v2, v3; // used to track vertices

	vec4 pl,  // position of the light
			 po,  // position of the occluder object/vertex
			 vlo, // vector from light to object
			 ps;  // position of the shadow

	float    rs,  // radius of shadow 
			 t;   // parameter t

	//////////////////////////////////////////////////////////////////////////
	// render model, this next section draws each copy of the mech model
	//////////////////////////////////////////////////////////////////////////

	// animate the model
	obj_md2->Animate();

	// extract the frame of animation from vertex banks
	obj_md2->ExtractFrame(obj_model);

	// set position of object 
	obj_model->SetWorldPos(0, 100, 0);

	// reset the object (this only matters for backface and object removal)
	obj_model->Reset();

	// create identity matrix
	mrot = mat4::Identity();

	// transform the local coords of the object
	obj_model->Transform(mrot, TRANSFORM_LOCAL_TO_TRANS,1);

	// perform world transform
	obj_model->ModelToWorld(TRANSFORM_TRANS_ONLY);

	// insert the object into render list
	_list->Insert(*obj_model,0);

	// set position of object 
	obj_model->SetWorldPos(0, 100, 200);

	// reset the object (this only matters for backface and object removal)
	obj_model->Reset();

	// create identity matrix
	mrot = mat4::Identity();

	// transform the local coords of the object
	obj_model->Transform(mrot, TRANSFORM_LOCAL_TO_TRANS,1);

	// perform world transform
	obj_model->ModelToWorld(TRANSFORM_TRANS_ONLY);

	// insert the object into render list
	_list->Insert(*obj_model,0);
	//////////////////////////////////////////////////////////////////////////


	//////////////////////////////////////////////////////////////////////////
	// draw all the light objects to represent the position of light sources

	// reset the object (this only matters for backface and object removal)
	obj_light_array[INDEX_RED_LIGHT_INDEX]->Reset();

	// set position of object to light
	obj_light_array[INDEX_RED_LIGHT_INDEX]->SetWorldPos(lights[POINT_LIGHT_INDEX].pos.x,
														lights[POINT_LIGHT_INDEX].pos.y,
														lights[POINT_LIGHT_INDEX].pos.z);

	// create identity matrix
	mrot = mat4::Identity();

	// transform the local coords of the object
	obj_light_array[INDEX_RED_LIGHT_INDEX]->Transform(mrot, TRANSFORM_LOCAL_TO_TRANS,1);

	// perform world transform
	obj_light_array[INDEX_RED_LIGHT_INDEX]->ModelToWorld(TRANSFORM_TRANS_ONLY);

	// insert the object into render list
	_list->Insert(*obj_light_array[INDEX_RED_LIGHT_INDEX],0);

	// reset the object (this only matters for backface and object removal)
	obj_light_array[INDEX_YELLOW_LIGHT_INDEX]->Reset();

	// set position of object to light
	obj_light_array[INDEX_YELLOW_LIGHT_INDEX]->SetWorldPos(lights[POINT_LIGHT2_INDEX].pos.x,
														   lights[POINT_LIGHT2_INDEX].pos.y,
														   lights[POINT_LIGHT2_INDEX].pos.z);
	// create identity matrix
	mrot = mat4::Identity();

	// transform the local coords of the object
	obj_light_array[INDEX_YELLOW_LIGHT_INDEX]->Transform(mrot, TRANSFORM_LOCAL_TO_TRANS,1);

	// perform world transform
	obj_light_array[INDEX_YELLOW_LIGHT_INDEX]->ModelToWorld(TRANSFORM_TRANS_ONLY);

	// insert the object into render list
	_list->Insert(*obj_light_array[INDEX_YELLOW_LIGHT_INDEX],0);

	////////////////////////////////////////////////////////////////////////////////////

	// reset number of polys rendered
	debug_polys_rendered_per_frame = 0;
	debug_polys_lit_per_frame = 0;

	// remove backfaces
	if (backface_mode)
		_list->RemoveBackfaces(*_cam);

	// apply world to camera transform
	_list->WorldToCamera(*_cam);

	// clip the polygons themselves now
	_list->ClipPolys(*_cam, CLIP_POLY_X_PLANE | CLIP_POLY_Y_PLANE | CLIP_POLY_Z_PLANE);

	// light scene all at once 
	if (lighting_mode)
	{
		lights.Transform(_cam->CameraMat(), TRANSFORM_LOCAL_TO_TRANS);
		_list->LightWorld32(*_cam);
	}

	// sort the polygon _list (hurry up!)
	if (zsort_mode)
		_list->Sort(SORT_POLYLIST_AVGZ);

	// apply camera to perspective transformation
	_list->CameraToPerspective(*_cam);

	// apply screen transform
	_list->PerspectiveToScreen(*_cam);

	// lock the back buffer
	graphics.LockBackSurface();

	// draw background
	background_bmp->Draw32(graphics.GetBackBuffer(), graphics.GetBackLinePitch(),0);

	// reset number of polys rendered
	debug_polys_rendered_per_frame = 0;

	if (wireframe_mode)
		_list->DrawWire32(graphics.GetBackBuffer(), graphics.GetBackLinePitch());
	else
	{
		RenderContext rc;

		// perspective mode affine texturing
		// set up rendering context
		rc.attr =    RENDER_ATTR_ZBUFFER  
			// | RENDER_ATTR_ALPHA  
			// | RENDER_ATTR_MIPMAP  
			// | RENDER_ATTR_BILERP
			| RENDER_ATTR_TEXTURE_PERSPECTIVE_AFFINE;

		// initialize _zbuffer to 0 fixed point
		_zbuffer->Clear(16000 << FIXP16_SHIFT);

		// set up remainder of rendering context
		rc.video_buffer   = graphics.GetBackBuffer();
		rc.lpitch         = graphics.GetBackLinePitch();
		rc.mip_dist       = 0;
		rc.zbuffer        = (unsigned char*)_zbuffer->Buffer();
		rc.zpitch         = WINDOW_WIDTH*4;
		rc.texture_dist   = 0;
		rc.alpha_override = -1;

		// render scene
		_list->DrawContext(rc);
	} // end if

	// now make second rendering pass and draw shadow

	// reset the render list
	_list->Reset();

	//////////////////////////////////////////////////////////////////////////
	// project shaded object into shadow by projecting it's vertices onto
	// the ground plane

	// reset the object (this only matters for backface and object removal)
	obj_model->Reset();

	// save the shading attributes/color of each polygon, and override them with
	// attributes of a shadow then restore them
	int pcolor[RenderObject::MAX_POLYS],    // used to store color
		pattr[RenderObject::MAX_POLYS];     // used to store attribute

	// save all the color and attributes for each polygon
	for (int pindex = 0; pindex < obj_model->PolyNum(); pindex++)
	{
		// save attribute and color
		pattr[pindex]  = obj_model->GetPolygon(pindex).attr;
		pcolor[pindex] = obj_model->GetPolygon(pindex).color;

		// set attributes for shadow rendering
		obj_model->GetPolygonRef(pindex).attr    = POLY_ATTR_RGB16 | POLY_ATTR_SHADE_MODE_CONSTANT | POLY_ATTR_TRANSPARENT;
		obj_model->GetPolygonRef(pindex).color   = _RGB32BIT(255,50,50,50) + (216 << 24);

	} // end for pindex

	// create identity matrix
	mrot = mat4::Identity();

	// solve for t when the projected vertex intersects ground plane
	pl = lights[POINT_LIGHT_INDEX].pos;

	// transform each local/model vertex of the object mesh and store result
	// in "transformed" vertex list, note 
	for (int vertex=0; vertex < obj_model->VerticesNum(); vertex++)
	{
		vec4 presult; // hold result of each transformation

		// compute parameter t0 when projected ray pierces y=0 plane
		vec4 vi;

		// set position of object 
		obj_model->SetWorldPos(0, 100, 0);

		// transform coordinates to worldspace right now...
		vi = obj_model->GetLocalVertex(vertex).v + obj_model->GetWorldPos();

		float t0 = -pl.y / (vi.y - pl.y);

		// transform point
		obj_model->GetTransVertexRef(vertex).v.x = pl.x + t0*(vi.x - pl.x);
		obj_model->GetTransVertexRef(vertex).v.y = 10.0; // pl.y + t0*(vi.y - pl.y);
		obj_model->GetTransVertexRef(vertex).v.z = pl.z + t0*(vi.z - pl.z);
		obj_model->GetTransVertexRef(vertex).v.w = 1.0;

	} // end for index

	// insert the object into render list
	_list->Insert(*obj_model,0);

	// and now second shadow object from second light source...

	// solve for t when the projected vertex intersects
	pl = lights[POINT_LIGHT_INDEX].pos; 

	// transform each local/model vertex of the object mesh and store result
	// in "transformed" vertex list
	for (int vertex=0; vertex < obj_model->VerticesNum(); vertex++)
	{
		vec4 presult; // hold result of each transformation

		// compute parameter t0 when projected ray pierces y=0 plane
		vec4 vi;

		// set position of object 
		obj_model->SetWorldPos(0, 100, 200);

		// transform coordinates to worldspace right now...
		vi = obj_model->GetLocalVertex(vertex).v + obj_model->GetWorldPos();

		float t0 = -pl.y / (vi.y - pl.y);

		// transform point
		obj_model->GetTransVertexRef(vertex).v.x = pl.x + t0*(vi.x - pl.x);
		obj_model->GetTransVertexRef(vertex).v.y = 10.0; // pl.y + t0*(vi.y - pl.y);
		obj_model->GetTransVertexRef(vertex).v.z = pl.z + t0*(vi.z - pl.z);
		obj_model->GetTransVertexRef(vertex).v.w = 1.0;

	} // end for index

	// insert the object into render list
	_list->Insert(*obj_model,0);

	// restore attributes and color
	for (int pindex = 0; pindex < obj_model->PolyNum(); pindex++)
	{
		// save attribute and color
		obj_model->GetPolygonRef(pindex).attr  = pattr[pindex];
		obj_model->GetPolygonRef(pindex).color = pcolor[pindex]; 

	} // end for pindex

	//////////////////////////////////////////////////////////////////////////

	// remove backfaces
	if (backface_mode)
		_list->RemoveBackfaces(*_cam);

	// apply world to camera transform
	_list->WorldToCamera(*_cam);

	// clip the polygons themselves now
	_list->ClipPolys(*_cam, CLIP_POLY_X_PLANE | CLIP_POLY_Y_PLANE | CLIP_POLY_Z_PLANE );

	// light scene all at once 
	if (lighting_mode)
	{
		lights.Transform(_cam->CameraMat(), TRANSFORM_LOCAL_TO_TRANS);
		_list->LightWorld32(*_cam);
	} // end if

	// sort the polygon list (hurry up!)
	if (zsort_mode)
		_list->Sort(SORT_POLYLIST_AVGZ);

	// apply camera to perspective transformation
	_list->CameraToPerspective(*_cam);

	// apply screen transform
	_list->PerspectiveToScreen(*_cam);

	// render the object

	if (wireframe_mode)
		_list->DrawWire32(graphics.GetBackBuffer(), graphics.GetBackLinePitch());
	else {
			// perspective mode affine texturing
			// set up rendering context
		RenderContext rc;
			rc.attr =    RENDER_ATTR_ZBUFFER  
						| RENDER_ATTR_ALPHA  
						// | RENDER_ATTR_MIPMAP  
						// | RENDER_ATTR_BILERP
						| RENDER_ATTR_TEXTURE_PERSPECTIVE_AFFINE;

			// initialize zbuffer to 0 fixed point
			//Clear_Zbuffer(&zbuffer, (16000 << FIXP16_SHIFT));

			// set up remainder of rendering context
			rc.video_buffer   = graphics.GetBackBuffer();
			rc.lpitch         = graphics.GetBackLinePitch();
			rc.mip_dist       = 0;
			rc.zbuffer        = (unsigned char*)_zbuffer->Buffer();
			rc.zpitch         = WINDOW_WIDTH*4;
			rc.texture_dist   = 0;
			rc.alpha_override = -1;

			// render scene
			_list->DrawContext(rc);
		} // end if

	// unlock the back buffer
	graphics.UnlockBackSurface();

	sprintf(work_string,"Lighting [%s]: Ambient=%d, Infinite=%d, Point=%d, Zsort [%s], BckFceRM [%s], Zsort[%s]", 
		(lighting_mode ? "ON" : "OFF"),
		lights[AMBIENT_LIGHT_INDEX].state,
		lights[INFINITE_LIGHT_INDEX].state, 
		lights[POINT_LIGHT_INDEX].state,
		(zsort_mode ? "ON" : "OFF"),
		(backface_mode ? "ON" : "OFF"),
		(zsort_mode ? "ON" : "OFF"));

	graphics.DrawTextGDI(work_string, 0+1, WINDOW_HEIGHT-34+1, RGB(0,0,0), graphics.GetBackSurface());
	graphics.DrawTextGDI(work_string, 0, WINDOW_HEIGHT-34, RGB(255,255,255), graphics.GetBackSurface());

	sprintf(work_string,"Polys Rendered: %d, Polys lit: %d Anim[%d]=%s Frm=%d", debug_polys_rendered_per_frame, debug_polys_lit_per_frame, obj_md2->_anim_state,MD2_ANIM_STRINGS[obj_md2->_anim_state], obj_md2->_curr_frame );
	graphics.DrawTextGDI(work_string, 0+1, WINDOW_HEIGHT-34-2*16+1, RGB(0,0,0), graphics.GetBackSurface());
	graphics.DrawTextGDI(work_string, 0, WINDOW_HEIGHT-34-2*16, RGB(255,255,255), graphics.GetBackSurface());

	sprintf(work_string,"CAM [%5.2f, %5.2f, %5.2f], CELL [%d, %d]",  _cam->Pos().x, _cam->Pos().y, _cam->Pos().z, cell_x, cell_y);
	graphics.DrawTextGDI(work_string, 0+1, WINDOW_HEIGHT-34-3*16+1, RGB(0,0,0), graphics.GetBackSurface());
	graphics.DrawTextGDI(work_string, 0, WINDOW_HEIGHT-34-3*16, RGB(255,255,255), graphics.GetBackSurface());

	// draw instructions
	graphics.DrawTextGDI("Press ESC to exit. Press <H> for Help.", 0, 0, RGB(0,255,0), graphics.GetBackSurface());

	// should we display help
	int text_y = 16;
	if (help_mode)
	{
		// draw help menu
		graphics.DrawTextGDI("<A>..............Toggle ambient light source.", 0, text_y+=12, RGB(255,255,255), graphics.GetBackSurface());
		graphics.DrawTextGDI("<I>..............Toggle infinite light source.", 0, text_y+=12, RGB(255,255,255), graphics.GetBackSurface());
		graphics.DrawTextGDI("<P>..............Toggle point light source.", 0, text_y+=12, RGB(255,255,255), graphics.GetBackSurface());
		graphics.DrawTextGDI("<W>..............Toggle wire frame/solid mode.", 0, text_y+=12, RGB(255,255,255), graphics.GetBackSurface());
		graphics.DrawTextGDI("<B>..............Toggle backface removal.", 0, text_y+=12, RGB(255,255,255), graphics.GetBackSurface());
		graphics.DrawTextGDI("<Z>..............Toggle Z-sorting.", 0, text_y+=12, RGB(255,255,255), graphics.GetBackSurface());
		graphics.DrawTextGDI("<1>,<2>..........Previous/Next Animation.", 0, text_y+=12, RGB(255,255,255), graphics.GetBackSurface());
		graphics.DrawTextGDI("<3>,<4>..........Play Animation Single Shot/Looped.", 0, text_y+=12, RGB(255,255,255), graphics.GetBackSurface());
		graphics.DrawTextGDI("<O>..............Select next object.", 0, text_y+=12, RGB(255,255,255), graphics.GetBackSurface());
		graphics.DrawTextGDI("<H>..............Toggle Help.", 0, text_y+=12, RGB(255,255,255), graphics.GetBackSurface());
		graphics.DrawTextGDI("<ESC>............Exit demo.", 0, text_y+=12, RGB(255,255,255), graphics.GetBackSurface());

	} // end help

	// flip the surfaces
	graphics.Flip(main_window_handle);

	// sync to 30ish fps
	Modules::GetTimer().Wait_Clock(30);

	// check of user is trying to exit
	if (KEY_DOWN(VK_ESCAPE) || Modules::GetInput().KeyboardState()[DIK_ESCAPE])
	{
		PostMessage(main_window_handle, WM_DESTROY,0,0);
	} // end if

}

}