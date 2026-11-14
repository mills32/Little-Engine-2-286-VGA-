/*

************************
*  LITTLE GAME ENGINE  *
************************
BUG: not found folders may crash the program
BUG: sprites may not delete bkg when being cloned and are drawn for the first time
	if the sprite spawms facing a solid or end of platform and flips just as spawms
##########################################################################
	
	A lot of code from David Brackeen                                   
	http://www.brackeen.com/home/vga/                                     

	Also Michael Abrash's Graphics Programming Black Book was very helpful.

	Sprite loader and bliter from xlib
	
	Please feel free to copy this source code.  
	
	This is a 16-bit program, Remember to compile in the COMPACT memory model.
	If your code becomes huge, you might have to use LARGE memory model.	
	
	All code is 80286 compatible, it could work on a 8088/8086 but it will be 4x slower
	and it woild loose the smooth 60 fps scrolling
	
	Do NOT define big arrays inside functions. If you must, only use text:
		- DON't use this: unsigned char text[] = "MY TEXT";
		- use this:       unsigned char *text  = "MY TEXT";
		this avoids the program trying to use libraries (which contain 
		"N_SCOPY@" function) and avoids making the code bigger. 
	
	LT_MODE = player movement modes
		MODE TOP = 0;
		MODE PLATFORM = 1;
	
	SPRITES:
		32x32 (7 sprites)
		64x64 (1  sprite:      20th, does not restore BKG)
	
	When you use two different sprites in a scene:
		LT_Set_AI_Sprites(A,B,0,0);
	If sprite "A" is at sprite[0], sprite B must be at least sprite[2].
	If the map is going to have more than one "A" sprite in a certain spot
	(for example 3), sprite "B" must be loaded at sprite[4] at least
	
	LT_Logo and all "Load" functions read a custom DAT format created for the engine.
	To load data from LDAT files:
			-write the DAT name in the first argument
			-write the file name in the second argument
	

	LANGUAGE SUPPORT

	Any that can store characters in one byte, configure your editor to support
	your language before saving c file.
	For english + most latin derived languages, use IBM50 or CP437 or.. whatever
	Just look for your language settings.
##########################################################################*/

//Library functions
#include "lt__eng.h"

//Main program
int running = 1;
int level_reset = 0;
int x,y = 0;
int i,j = 0;
int Scene = 0;
int menu_option = 0;
int menu_pos[10] = {108-16,116-16,124-16,132-16,140-16,148-16,156-16};
int game = 0;
int random;
int MUSIC_MODE = 0;

//Sprite animations, you have up to 8 animations per sprite, 8 frames each. they will always loop at 8th frame
//AI sprites and player sprite have their first 6 animations resrved for common movements 

//Little robot animations
byte Player_Animation[] = {	//Animations for player are fixed at these offsets
	16,16,16,16,17,17,17,17,	// 0 UP
	18,18,18,18,19,19,19,19,	// 1 DOWN
	8,9,10,11,12,13,14,15,		// 2 LEFT
	0,1,2,3,4,5,6,7,			// 3 RIGHT
	25,25,25,25,25,25,25,25,	// 4 JUMP/FALL LEFT
	24,24,24,24,24,24,24,24,	// 5 JUMP/FALL RIGHT
	20,20,20,21,21,22,22,23,	// 6 custom 
	26,27,28,29,29,28,27,26,	// 7 custom 
	
};

byte Cursor_Animation[] = {0,1,2,3,4,5,6,7};
byte Ship_Animation[] = {//
	0,1,0,1,0,1,0,1,
	0,0,1,0,1,0,1,0,
	0,0,0,0,1,1,1,1,
	0,0,0,0,1,1,1,1,
	0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,
	0,0,1,1,2,2,2,3,
	0,0,0,0,1,1,1,1,
};

byte Enemy0_Animation[] = {//
	0,0,1,1,0,0,1,1,
	2,2,3,3,2,2,3,3,
	4,4,5,5,4,4,5,5,	
	6,6,7,7,6,6,7,7,		
	0,0,0,0,0,0,0,0,	
};

byte Enemy2_Animation[] = {//Walking bots
	0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,
	4,5,6,7,7,6,5,4,
	0,1,2,3,3,2,1,0,
	0,0,0,0,0,0,0,0,	
};

byte Enemy3_Animation[] = {//Walking bots
	0,0,0,0,1,1,1,1,
	2,2,2,2,3,3,3,3,
	4,4,4,4,5,5,5,5,
	6,6,6,6,7,7,7,7,
	0,0,0,0,0,0,0,0,
};

byte Rocket_Animation[] = {0,1,0,2,0,1,0,2};


//Background animations, only 128x128, 12 frames max
// all animations loaded in a scene share the same 12 frames
// You can define one 12 frame animation, or two 6 frame animations, or six two frame animations
byte tortoise_looking_animation[] = {
	18,//number of frames
	0,0,0,1,2,2,2,1,0,3,4,4,4,5,4,4,4,3 //animation frames
};

byte tortoise_scared_animation[] = {
	31,//number of frames
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	6,7,8,
	9,9,9,9,9,9,9,9,9,9,9,9,
	10,11 //animation frames
};

//SFX declare these outside functions to avoid "N_SCOPY@" error /////////
byte Speaker_Menu[16] = {56,52,51,45,40,30,25,15,10,5,0,0,0,0,0,0};
byte Speaker_Select[16] = {90,80,70,60,50,30,10,5,10,30,50,60,70,60,50,30};
byte Adlib_Bell_Sound[11] = {0x02,0x07,0x02,0x00,0xF5,0x93,0xCB,0xF0,0x00,0x00,0x01};
byte Adlib_Select_Sound[11] = {0x62,0x04,0x04,0x1E,0xF7,0xB1,0xF0,0xF0,0x00,0x00,0x03};
byte Adlib_PacMan_Sound[11] = {0xF9,0xF3,0x00,0x94,0xF3,0x91,0xF0,0xF1,0x0E,0x00,0x03};
byte Adlib_Metal[] = {0x00,0x01,0x00,0x80,0xF8,0xF0,0xF0,0xF0,0x00,0x00,0x01,0x00};
byte Speaker_Crash[16] = {69,3,120,32,39,200,20,60,16,106,12,87,8,70,32,60};
byte Speaker_Jump[16] = {30,35,40,43,44,45,46,47,48,49,50,51,52,53,54,55};
byte Speaker_Get_Item[16] = {69,70,71,72,69,64,58,40,30,12,12,16,20,70,32,60};
byte Speaker_Get_Item_Adlib[] = {0xC8,0xF2,0x00,0x10,0xE6,0xC4,0x4C,0x40,0x0A,0x02,0x00};


//Custom functions at the end of this file
void Display_Intro();
void Load_Platform();
void Run_Platform();
void Load_Adventure();
void Run_Adventure();

//Main function
void main(){

	//Allways the first, loads setup and initializes engine
	LT_Setup();	
	//If you want a logo
	//LT_Logo("ASSETS/IMAGE.DAT","logo.bmp");

	//That's it, let's play
	LT_MODE = 0;
	game = 0;
	//LT_EGA_SPRITES_TRANSLUCENT = 1;
	//MENU
	while (running){
		switch (game){
			case 0: Display_Intro(); break;
			case 1: Load_Platform(); game = 2; break;
			case 2: Run_Platform(); break;
			case 3: Load_Adventure(); game = 4; break;
			case 4: Run_Adventure(); break;
		} 
	}
	LT_ExitDOS();
	
}

//custom function
void Sprite_Bounce_Left(byte spr){
	//Bounce away the sprite
	sprite[spr].pos_x-=2;LT_Update(0,0);
	sprite[spr].pos_x-=2;sprite[spr].pos_y-=2;LT_Update(0,0);
	sprite[spr].pos_x-=2;sprite[spr].pos_y-=1;LT_Update(0,0);
	sprite[spr].pos_x-=2;sprite[spr].pos_y+=3;LT_Update(0,0);
}

//Scenes/levels
void LT_Update_column(unsigned short x, unsigned short y, unsigned short map_offset);
void Display_Intro(){
	int key_timmer = 0;
	int tortoise_timmer = 0;
	Scene = 1;
	menu_option = 0;
	level_reset = 0;
	SCR_X = 0;SCR_Y = 0;
	
	LT_SPRITE_MODE = 0;
	LT_IMAGE_MODE = 1;
	LT_ENDLESS_SIDESCROLL = 0;
	
	//Enable this to load stuff, it will fade out automatically
	//also stops the music and shows a cute animation
	LT_Start_Loading();
		LT_Load_Image("ASSETS/IMAGE.DAT","menu.bmp"); //Load a 320x200 bkg image
		LT_Load_Animation("ASSETS/ANIM.DAT","menu_ani.bmp"); // Load tortoise anim to VRAM (12 frames)
		LT_Load_Sprite("ASSETS/SPRITE.DAT","cursor.bmp",0,Cursor_Animation);
		if (LT_GET_MUSIC() == 1) LT_Load_Music("ASSETS/MUSIC/menu.vgm",0);
		if (LT_GET_MUSIC() == 2) LT_Load_Music("ASSETS/MUSIC/andy.mod",0);
	LT_End_Loading(); //End loading

	LT_MODE = 0;
	
	//Init Sprite at positions
	LT_Reset_Sprite_Stack();
	LT_Init_Sprite(0,32,101);
	LT_Set_Sprite_Animation(0,0);
	LT_Set_Sprite_Animation_Speed(0,6);

	LT_Start_Music();
	while (Scene == 1) {
		if (key_timmer) key_timmer--;
		//Select 
		if (LT_Keys[LT_UP] && menu_option && !key_timmer) {
			if (LT_SFX_MODE == 0) LT_Play_PC_Speaker_SFX(Speaker_Menu);
			if (LT_SFX_MODE == 1) LT_Play_AdLib_SFX(Adlib_Select_Sound,8,2,2);
			menu_option--;
			sprite[0].pos_y-=16;
			key_timmer = 32;
		}
		if (LT_Keys[LT_DOWN] && menu_option < 2  && !key_timmer) {
			if (LT_SFX_MODE == 0) LT_Play_PC_Speaker_SFX(Speaker_Menu);
			if (LT_SFX_MODE == 1) LT_Play_AdLib_SFX(Adlib_Select_Sound,8,2,2);
			menu_option++;
			sprite[0].pos_y+=16;
			key_timmer = 32;
		}
		
		if (LT_Keys[LT_ACTION]) {
			if (menu_option == 0){Scene = 2;game = 1;}
			if (menu_option == 1){Scene = 2;game = 3;}
			if (menu_option == 2){Scene = 2;game = 255; running = 0;}
		}
		if (LT_Keys[LT_ESC]) {Scene = 2;game = 255; running = 0;}

		/////////////
		if (tortoise_timmer < 8*60) LT_Draw_Animation(0,160,56,tortoise_looking_animation,20);
		else LT_Draw_Animation(0,160,56,tortoise_scared_animation,10);
		tortoise_timmer++;
		if (tortoise_timmer == 16*60) tortoise_timmer = 0;

		//LT_Update_column(0,0,0);
		LT_Play_Music();
		LT_Update(0, 0);
	}
}

byte Level_cards = 0;
void Load_Platform(){
	LT_SPRITE_MODE = 1;
	LT_Start_Loading(); 
		if (LT_GET_MUSIC() == 1) LT_Load_Music("ASSETS/music/music.vgm",0);
		if (LT_GET_MUSIC() == 2) LT_Load_Music("ASSETS/music/sam.mod",0);
		LT_Load_Map("ASSETS/MAP.DAT","platform.tmx");
		LT_Load_Sprite("ASSETS/SPRITE.DAT","player.bmp",0,Player_Animation);
		LT_Load_Sprite("ASSETS/SPRITE.DAT","bot1.bmp",1,Enemy2_Animation);
		LT_Load_Tiles("ASSETS/TILESETS.DAT","plat_til.bmp");
	LT_End_Loading();
	
	LT_Set_Sprite_Animation_Speed(0,2);
	LT_Set_Sprite_Animation_Speed(1,7);
	
	Scene = 2;
	Level_cards = 0;
}

void Run_Platform(){
	int n;
	byte last_cards = 0;
	int dying = 0;
	Scene = 2;
	
	//Init_Player
	LT_Reset_Sprite_Stack();
	LT_Init_Sprite(0,32,13*16);
	LT_Set_AI_Sprites(1,1);

	LT_Set_Map(2,8);

	LT_MODE = 1;
	LT_Set_Sprite_Plane(LT_PLANES_ALL);
	LT_IMAGE_MODE = 0;
	LT_ENEMY_DIST_X = 23;
	LT_ENEMY_DIST_Y = 24;
	if(level_reset == 0){
		LT_Draw_Text_Box(1,1,36,4,3,LT_ACTION,0,"Just A Platform game test, I createdthe engine for this kind of game.   Collect items and reach the end.    PRESS ACTION.");
		LT_Start_Music();
	}
	while (Scene == 2){
		if (!dying){
			//In this mode, sprite is controlled using L R and Jump
			LT_move_player(0);
		}
		//EDIT MAP: GET cards
		switch (LT_Player_State[SPR_TILP_NUM]){
			case 171: LT_Sprite_Edit_Map(0, 0, 0); Level_cards++; break;
			case 172: LT_Sprite_Edit_Map(0, 3, 0); Level_cards++; break;
			case 173: LT_Sprite_Edit_Map(0,43, 0); Level_cards++; break;
			case 174: LT_Sprite_Edit_Map(0,40, 0); Level_cards++; break;
			case 175: LT_Sprite_Edit_Map(0,19, 0); Level_cards++; break;
		}
		if (Level_cards != last_cards){
			if (LT_SFX_MODE == 0)LT_Play_PC_Speaker_SFX(Speaker_Get_Item);
			if (LT_SFX_MODE == 1)LT_Play_AdLib_SFX(Speaker_Get_Item_Adlib,8,4,0);
		}
		last_cards = Level_cards;
		
		if (LT_Player_State[SPR_JUMP]){
			if (LT_SFX_MODE == 0)LT_Play_PC_Speaker_SFX(Speaker_Jump);
			if (LT_SFX_MODE == 1);
		}
		
		//If collision with breakable tile (door in this case)
		if (LT_Player_State[SPR_COL_X] == 2){
			if (Level_cards < 8) LT_Draw_Text_Box(4,10,17,2,3,LT_ACTION,0,"YOU NEED 8 CARDS TO OPEN DOOR.   @");
			else { //Open door
				if (LT_SFX_MODE == 0)LT_Play_PC_Speaker_SFX(Speaker_Get_Item);
				if (LT_SFX_MODE == 1)LT_Play_AdLib_SFX(Adlib_Metal,8,4,0);
				Sprite_Bounce_Left(0);
				//and update/open Door tile
				LT_Edit_MapTile(sprite[0].tile_x+1,sprite[0].tile_y-1,124,0);
				LT_Edit_MapTile(sprite[0].tile_x+2,sprite[0].tile_y-1,125,0);
				LT_Edit_MapTile(sprite[0].tile_x+1,sprite[0].tile_y,126,0);
				LT_Edit_MapTile(sprite[0].tile_x+2,sprite[0].tile_y,127,0);
				LT_Wait(1);
			}
		}
		
		//If collision tile = ?, end level
		if ((LT_Player_State[SPR_TILC_NUM] == 11)||(LT_Keys[LT_ESC])){Scene = 1; game = 0;}

		//Info window
		if (LT_Keys[LT_ACTION]){
			unsigned char *text = "  INFO  WINDOW  LIVES 3 CARDS 0   A@OK  B@EXIT  ";
			text[30] = Level_cards+48;
			if (LT_Draw_Text_Box(10,8,16,3,4,LT_ACTION,LT_JUMP,text)){Scene = 1; game = 0;}
		}

		//if water or enemy, reset level
		if (sprite[0].pos_y > (34*16)){
			if (LT_SFX_MODE == 0){LT_Play_PC_Speaker_SFX(Speaker_Crash);}
			if (LT_SFX_MODE == 1)LT_Play_AdLib_SFX(Speaker_Get_Item_Adlib,8,4,0);
			Scene = 1;
			game = 2;
			level_reset = 1;
			LT_sleep(1);
			LT_Fade_out();
		}
		if (LT_Player_Col_Enemy()){dying = 1;}//LT_Set_Sprite_Animation(16,0);
		
		if (dying){
			if (LT_SFX_MODE == 0){LT_Play_PC_Speaker_SFX(Speaker_Crash);}
			if (LT_SFX_MODE == 1);
			Scene = 1;
			game = 2;
			level_reset = 1;
			LT_sleep(1);
			LT_Fade_out();			
		}
		
		LT_Cycle_palette(10,3,4,1);
		LT_Play_Music();
		LT_Update(1,0);
	}
}

void Load_Adventure(){
	LT_SPRITE_MODE = 1;
	LT_Start_Loading();
	if (LT_GET_MUSIC() == 1) LT_Load_Music("ASSETS/music/music.vgm",0);
	if (LT_GET_MUSIC() == 2) LT_Load_Music("ASSETS/music/sam.mod",0);
	LT_Load_Map("ASSETS/MAP.DAT","adventur.tmx");
	LT_Load_Sprite("ASSETS/SPRITE.DAT","player2.bmp",0,Player_Animation);
	LT_Load_Sprite("ASSETS/SPRITE.DAT","bot1.bmp",1,Enemy2_Animation);
	LT_Load_Sprite("ASSETS/SPRITE.DAT","bot2.bmp",4,Enemy3_Animation);
	LT_Load_Tiles("ASSETS/TILESETS.DAT","adv_til.bmp");
	LT_End_Loading();

	LT_Set_Sprite_Animation_Speed(0,2);
	//LT_Set_Sprite_Animation_Speed(1,7);

	Scene = 2;
}

void Run_Adventure(){
	int n;
	Scene = 2;

	//Init_Player
	LT_Reset_Sprite_Stack();
	LT_Init_Sprite(0,19*16,32*16);
	LT_Set_AI_Sprites(1,0);

	LT_Set_Map(8,20);

	LT_MODE = 0;
	LT_Set_Sprite_Plane(LT_PLANES_4x3);
	LT_IMAGE_MODE = 0;

	LT_Draw_Text_Box(8,6,22,4,3,LT_ACTION,0,"Sample adventure game.PRESS ACTION.");
	LT_Start_Music();

	while (Scene == 2){
		//In this mode, sprite is controlled using L R and Jump
		LT_move_player(0);

		//Info window
		if (LT_Player_State[SPR_COL_X] == 2){
			unsigned char *text = "IS CLOSED. PRESS ACTION";
			LT_Draw_Text_Box(12,27,23,1,4,LT_ACTION,0,text);
		}

		if ((LT_Player_State[SPR_TILC_NUM] == 11)||(LT_Keys[LT_ESC])){Scene = 1; game = 0;}

		LT_Play_Music();
		LT_Update(1,0);
	}
}

