/*Compile And run sprites functions from XLIB - Mode X graphics library
; Written By Themie Gouthas                  
; Aeronautical Research Laboratory           
; Defence Science and Technology Organisation
; Australia                                  
;
; egg@dstos3.dsto.gov.au
; teg@bart.dsto.gov.au
*/

#include "lt__eng.h"

#define SC_INDEX            0x03c4    // VGA sequence controller
#define SC_DATA             0x03c5

#define GC_INDEX            0x03ce    // VGA graphics controller
#define GC_DATA             0x03cf

#define MAP_MASK            0x02
#define ALL_PLANES          0xff02
 

SPRITE far *sprite;

//Player
byte tile_number = 0;		//Tile a sprite is on
byte tilecol_number = 0;	//Collision Tile a sprite is on
byte tilecol_number_down = 0;//Collision Tile down
byte tile_number_VR = 0;	//Tile vertical right
byte tile_number_VL = 0;	//Tile vertical left
byte tile_number_HU = 0;	//Tile horizontal up
byte tile_number_HD = 0;	//Tile horizontal down

byte LT_Sprite_Stack = 0;
byte LT_Sprite_Stack_Table[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
byte LT_AI_Enabled = 0;
byte LT_Active_AI_Sprites[] = {0,0,0,0,0,0,0};
byte Lt_AI_Sprite[] = {0,0,0,0,0,0,0,0};
byte LT_Sprite_Size_Table[] = {8,8,8,8,8,8,8,8,16,16,16,16,16,16,16,16,32,32,32,32,64};


byte far *sprite_id_table;
byte sprite_size = 2;

int LT_SPRITE_PLAYER = 0;
byte selected_AI_sprite;
int LT_number_of_ai = 0;
unsigned char far *LT_sprite_data;
extern byte LT_AI_Sprite[];
extern byte far *LT_map_collision;
extern word far *LT_map_data;
dword LT_sprite_data_offset;
void LT_Error(char *error, char *file);
extern byte *LT_Filename;
extern word LT_VRAM_Logical_Width;
void LT_VGA_Enable_4Planes();
void LT_VGA_Return_4Planes();
extern word TILE_VRAM;
extern word LT_FrameSkip;
extern byte tga_page;
extern byte LT_flipscroll;
extern word pageflip[];
extern word gfx_draw_page;
//player movement modes
//MODE TOP = 0;
//MODE PLATFORM = 1;
byte LT_SPRITES_PLANE_MODE = 0x0F;
byte LT_MODE = 0;
byte LT_ENEMY_DIST_X = 15;
byte LT_ENEMY_DIST_Y = 15;
byte LT_Player_Dir = 0;
byte LT_Slope_Timer = 0; //A dirty way of making big slopes or long conveyor belts impossible to cross

// Player variables: 
//tile, collision tile. collision x-y,hit,jump,action,move,ground
//move = 0 1 2 3 4 = Stop U D L R 
byte LT_Player_State[9] = {0,0,0,0,0,0,0,0,0};

//empty frame, to avoid crashing if not loaded sprites used
byte LT_Sprite_Empty_Frame[] = {0xCB,0xCB,0xCB,0xCB}; //far return instruction

int x_adjust = 160;
int LT_Jump_Release = 1;
// Parabolic jump: 39 frames up, 39 frames down. Falling starts at frame 39.
int LT_player_jump_pos[] = {
	-5,-6,-5,-5,-4,-5,-5,-4,-4,-4,-4,-4,-3,-3, -4,-3,-4,-3,
	-4,-3,-3,-2,-3,-2,-3,-2,-2,-2,-1,-2,-1,-1,
	-1,-1,-1, 0,-1, 0, 0,
	0, 0, 1, 0, 1, 1, 1, 1, 1, 2, 1, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 3, 2, 2, 3, 2, 3, 3, 3, 4, 3, 3, 4, 4,
	3, 4, 4, 3, 0
};

int LT_Spr_speed_float = 0;
int far LT_Spr_speed[] = {
	//Add speeds for
	-2,-2,-3,-2,-2,-3,-2,-2,
	-2,-2,-2,-2,-1,-2,-2,-2,
	-2,-2,-2,-1,-1,-2,-2,-2,
	-2,-1,-2,-1,-2,-2,-2,-1,
	-2,-1,-2,-1,-2,-1,-2,-1,
	-2,-1,-1,-2,-1,-1,-2,-1,
	-2,-1,-1,-1,-2,-1,-1,-1,
	-2,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1, 0,-1,-1,-1,
	-1,-1,-1, 0, 0,-1,-1,-1,
	-1, 0,-1,-1,-1, 0,-1, 0,
	-1, 0,-1, 0,-1, 0,-1, 0,
	-1, 0, 0,-1, 0, 0,-1, 0,	
	-1, 0, 0, 0,-1, 0, 0, 0,
	-1, 0, 0, 0, 0, 0, 0, 0,
	
	0,0,0,0,0,0,0,0,	//8x16
	
	1,0,0,0,0,0,0,0,
	1,0,0,0,1,0,0,0,
	1,0,0,1,0,0,1,0,
	1,0,1,0,1,0,1,0,
	1,0,1,1,1,0,1,0,
	1,1,1,0,0,1,1,1,
	1,1,1,1,0,1,1,1,
	1,1,1,1,1,1,1,1,
	2,1,1,1,1,1,1,1,
	2,1,1,1,2,1,1,1,
	2,1,1,2,1,1,2,1,
	2,1,2,1,2,1,2,1,
	2,1,2,1,2,2,2,1,
	2,2,2,1,1,2,2,2,
	2,2,2,2,1,2,2,2,
	2,2,3,2,2,3,2,2,	//8x32
};


int LT_wmap = 0;
int LT_hmap = 0;//91.888
int LT_Scroll_Camera_float = 0;
int LT_Scroll_Camera_array[135] = {
	0,1,1,2,2,2,3,3,3,3,4,4,4,4,4,5,5,5,5,5,5,6,6,6,6,6,6,6,7,7,7,7,7,7,7,7,
	8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
};

int LT_Scroll_Camera_speed_x[] = {
	0,0,0,0,0,0,0,0,
	1,0,0,0,1,0,0,0,
	1,0,1,0,1,0,1,0,
	1,1,1,0,1,1,1,0,
	1,1,1,1,1,1,1,1,
	2,1,1,1,2,1,1,1,
	2,1,2,1,2,1,2,1,
	2,2,2,1,2,2,2,1,
	2,2,2,2,2,2,2,2,
};

int LT_Scroll_Camera_speed_y[] = {
	0,0,0,0,1,0,0,0,
	1,0,1,0,1,0,1,0,
	1,1,1,1,1,1,1,1,
	1,1,1,2,1,1,1,2,
	2,1,2,1,2,1,2,1,
	2,2,2,2,2,2,2,2,
	3,2,2,2,3,2,2,2,
	3,2,3,2,3,2,3,2,
	3,3,3,3,3,3,3,3,
};

int _abs(int x){//64.412
	int y = x >>15;
	return (int)((x ^ y) - y);
}

void _memcpy(void *dest, void *src, word number);

void LT_scroll_follow(int sprite_number){
	SPRITE *s = &sprite[sprite_number];
	int x;
	int x1;
	int y = (s->pos_y-SCR_Y)-120;
	int y1 = _abs(y);
	int speed_x = 0;
	int speed_y = 0;
	LT_SPRITE_PLAYER = sprite_number;
	if ((s->state == 3) && (x_adjust != 230))x_adjust++; //FACING LEFT
	if ((s->state == 4) && (x_adjust !=  90))x_adjust--; //FACING RIGHT

	//Show more screen in the direction the sprite is facing
	x = (s->pos_x-SCR_X) - x_adjust;
	x1 = _abs(x);

	if ((SCR_X > -1) && ((SCR_X+319)<LT_wmap) && (SCR_Y > -1) && ((SCR_Y+240)<(LT_hmap))){
		if (LT_Scroll_Camera_float == 8) LT_Scroll_Camera_float = 0;
		speed_x = LT_Scroll_Camera_speed_x[(LT_Scroll_Camera_array[x1]<<3)+LT_Scroll_Camera_float];
		speed_y = LT_Scroll_Camera_speed_y[(LT_Scroll_Camera_array[y1]<<3)+LT_Scroll_Camera_float];
		if (x < 0) SCR_X-=speed_x;
		if (x > 0) SCR_X+=speed_x;
		if (y < 0) SCR_Y-=speed_y;
		if (y > 0) SCR_Y+=speed_y;
		
		LT_Scroll_Camera_float++;
	}

	if (SCR_X < 0) SCR_X = 0; 
	if ((SCR_X+320) > LT_wmap) SCR_X = LT_wmap-320;
	if (SCR_Y < 0) SCR_Y = 0; 
	if ((SCR_Y+241) > (LT_hmap)) SCR_Y = LT_hmap-241;
}

void LT_Set_Sprite_Plane(byte planes){ LT_SPRITES_PLANE_MODE = planes;}

//load sprites with transparency (size = 8,16,32)
byte spr_line[8+64+8] = {0};
void LT_Load_Sprite(char *file, char *dat_string, int sprite_number, byte *animations){
	SPRITE *s = &sprite[sprite_number];
	unsigned char type = 0;
	word frame = 0;
	word size;
	byte n = 0;
	word f; //file

	LT_Filename = file;
	f = LT_fopen(file,0);
	if(!f) LT_Error("Can't find$",file);
	DAT_Seek2(f,dat_string);
	LT_fread(f,1,&type);
	if(type == 19) ;//32x32
	else if(type == 15) ;//64x64
	else LT_Error("Not a sprite $",file);
	LT_fread(f,1,&n);
	s->nframes = n;
	//read frames
	for (frame = 0; frame < n; frame++){
		if(!LT_fread(f,2,&size)) LT_Error("sprite size error$",file);
		if (!size) LT_Error("sprite size error$",file);
		if (LT_sprite_data_offset + size > 0xFFFF) LT_Error("Not enough RAM for sprites $",file);
		if(!LT_fread(f,size,&LT_sprite_data[LT_sprite_data_offset])) LT_Error("sprite reading error$",file);
		s->frames[frame] = &LT_sprite_data[LT_sprite_data_offset];
		LT_sprite_data_offset+=size;
	}
	
	//IINIT SPRITE
	//Load animations
	_memcpy(s->animations,animations,64);
	//Check animations so that game does not crash
	for (frame = 0; frame < 64;frame++){
		if (s->animations[frame] > s->nframes-1) s->animations[frame] = 0;
	}
	//s->bkg_data set in init function (sys.c)
	s->init = 0;
	s->frame = 0;
	s->baseframe = 65;
	s->ground = 0;
	s->jump = 1;
	s->jump_frame = 39;
	s->climb = 0;
	s->animate = 0;
	s->anim_speed = 0;
	s->speed = 4;
	s->anim_counter = 0;
	s->speed_x = 8*16;
	s->speed_y = 8*16;
	s->s_x = 0;
	s->s_y = 0;
	s->id_pos = 0;
	s->s_delete = 0;
	s->fixed_sprite_number = 0;
	s->mspeed_x = 1;
	s->mspeed_y = 1;
	s->get_item = 0;
	s->mode = 0;
	s->ai_stack = 0;

	if (type == 15) s->mode = 3;//64x64, no restoring background
}

void LT_Clone_Sprite(int sprite_number_c,int sprite_number){
	SPRITE *c = &sprite[sprite_number_c];
	SPRITE *s = &sprite[sprite_number];
	int j;
	if (c->mode == 3) return;
	c->nframes = s->nframes;
	for(j=0;j<c->nframes;j++) {
		c->frames[j] = s->frames[j];
	}
	
	//sprite[sprite_number_c].bkg_data  //allocated in lt_sys
	_memcpy(c->animations,s->animations,32);
	c->init = 0;
	c->frame = 0;
	c->baseframe = 65;
	c->ground = 0;
	c->jump = 2;
	c->jump_frame = 0;
	c->climb = 0;
	c->animate = 0;
	c->anim_speed = 0;
	c->speed = s->speed;
	c->anim_counter = 0;
	c->mspeed_x = 1;
	c->mspeed_y = 1;
	c->speed_x = 8*16;
	c->speed_y = 8*26;
	c->s_x = 0;
	c->s_y = 0;
	c->id_pos = 0;
	c->s_delete = 0;
	c->fixed_sprite_number = 0;
	c->get_item = 0;
	c->mode = 0;
	c->ai_stack = 0;
}

void LT_Init_Sprite(int sprite_number,int x,int y){
	SPRITE *s = &sprite[sprite_number];
	LT_Sprite_Stack_Table[LT_Sprite_Stack] = sprite_number;
	s->pos_x = x;
	s->pos_y = y;
	s->ai_stack = LT_Sprite_Stack;
	s->init = 0;
	LT_Sprite_Stack++;
	LT_Player_Dir = 3;
	s->jump = 1;
	s->jump_frame = 39;
}

void LT_Add_Sprite(int sprite_number,word x, word y) {
	sprite[sprite_number].pos_x = x;
	sprite[sprite_number].pos_y = y;
}

void LT_Reset_Sprite_Stack(){
	int i;
	LT_Sprite_Stack = 0;
	for (i = 0; i<33; i++) LT_Sprite_Stack_Table[i] = 0;
	LT_AI_Sprite[0] = 0;
	LT_memset(sprite_id_table,0,36*512);
}

void LT_Set_Sprite_Animation(int sprite_number, byte anim){
	SPRITE *s = &sprite[sprite_number];
	if (s->baseframe != anim<<3){//If changed animation
		s->baseframe = anim<<3;
		s->anim_counter = anim<<3;
		s->animate = 1;
		if (s->baseframe > 64) s->baseframe = 0;
	}
}

void LT_Set_Sprite_Animation_Speed(int sprite_number, byte speed){
	SPRITE *s = &sprite[sprite_number];
	s->speed = speed;
}

void LT_Sprite_Stop_Animation(int sprite_number){
	if (sprite[sprite_number].animate == 1){
		sprite[sprite_number].animate = 0; 
		sprite[sprite_number].baseframe = 65;
	}
}


////////////////////////////////////////////
//  HARDWARE BG and FG PLANES FOR SPRITES //
////////////////////////////////////////////
//
// Numbers represent background colors
// ABCDEFG: represent foreground colors
//
// MODE 0: 8 colors for sprite and tiles + 1 foreground color
//		PLANE MASK = 0x07; can look great for fog
//
// 		0,1,2,3,4,5,6,7,A,A,A,A,A,A,A,A
//
// MODE 1: 4 colors for sprite and background + 3 foreground colors
//		PLANE MASK = 0x03; Can look great for cave level
//
//		0,1,2,3,A,A,A,A,B,B,B,B,C,C,C,C
//
// MODE 2: 2 colors for sprite and background + 7 foreground colors
//		PLANE MASK = 0x01; Can look great for very dark cave level
//
//		0,1,A,A,B,B,C,C,D,D,E,E,F,F,G,G
//


//Fastest method of drawing 16 color sprites on EGA/VGA
//Every color needs one pass, but as long as you don't use incredibly detailed images
//every pass will be way smaller than the previous one.
//Actual compiled sprite method from Michael Abrash's Graphics Programming Black Book
void run_compiled_sprite(int Pos,unsigned char rot, char *Sprite){
	unsigned char left_mask = 0xff >> rot;
	unsigned char right_mask = 0xff << (8-rot);
	//This settings allow the use of "and byte ptr es:[di+word],byte"
	//  a single instruction which reads a latch and pastes the sprite byte
	asm{
		pusha; push ds;
		//mov dx,0x03C4; mov ax,0x0F02; out dx,ax;   // map mask: all planes
		mov dx,0x03CE;
		mov ax,0x0007; out dx,ax;                   // Color Don't Care = 0 -> reads return FFh
		mov ax,0x0B05; out dx,ax;                   // write mode 3, read mode 1
		mov al,0x03; mov ah,rot; out dx,ax;         // rotate count

		mov ax,0xA000; mov es,ax; mov di,Pos;       //VRAM dest

		mov al,0x08; mov ah,left_mask; out dx,ax;   // left mask first
		mov al,0x00; out dx,al; inc dx;             // select Set/Reset reg, dx = 03CF

		//Fron XLIB, *Sprite is correctly passed to asm call if done like this.
		//We do this so the compiled shape won't need  segment overrides, copied from.
		mov bx,0xA000; mov ds,bx
		mov bh,right_mask;          // store mask in bh

		mov cx,2;//two passes left/right
	}

	asm call dword ptr [Sprite];

	_end:
	asm{
		// restore defaults
		mov ax,0xFF08; out dx,ax;
		mov ax,0x0003; out dx,ax;
		mov ax,0x0005; out dx,ax;
		mov ax,0x0F07; out dx,ax;
		pop ds; popa;
	}

}


int LT_Sprite_Stack_Table_Pos0 = 0;
//Draw restoring bkg, slower
extern unsigned char Enemies;
//If pos -2 (frame-2), then Clean bkg at pos-2 in draw page (for all sprites)
//If pos -1 (frame-1), pick bkg at pos -1 from the clean draw page.
//paste at current pos on draw page (if first paste, only do this)

void (*LT_Draw_Sprites)(void);

void LT_Draw_Sprites_1(){
	int sprite_number;
	int page_y = pageflip[gfx_draw_page&1];
	//goto _debug;
	LT_VGA_Enable_4Planes();

	//RESTORE SPRITE BKG's
	for (sprite_number = 0; sprite_number < LT_Sprite_Stack; sprite_number++){	
		SPRITE *s = &sprite[LT_Sprite_Stack_Table[sprite_number]];
		int x = s->pos_x;
		int y = s->pos_y;
		int lx = s->last_last_x;
		int ly = s->last_last_y+page_y;

		if (s->mode == 3) continue;

		//ONLY IF IT IS INSIDE THE ACTIVE MAP
		if ((x > SCR_X-30)&&(x < SCR_X+320)&&(y > SCR_Y-29)&&(y < SCR_Y+243)){
			if (s->init == 2){
			word bkg_data = s->bkg_data;
			word screen_offset0;
			//Paste at pos -2
			screen_offset0 = (ly*50)+(lx>>3);
			///Clean pos 0 bkg
			asm{
				push ds;push di;push si;

				mov dx,0x03C4; mov ax,0x0F02; out dx,ax;
				mov dx,0x03CE;
				mov ax,0x0205; out dx,ax;
				mov ax,0x0008; out dx,ax;

				mov ax,0A000h; mov es,ax; mov di,screen_offset0; //es:di destination vram
				mov	ds,ax; mov si,bkg_data	//ds:si source vram
				mov bx,50-5;

				movsb;movsb;movsb;movsb;movsb;add di,bx;movsb;movsb;movsb;movsb;movsb;add di,bx;
				movsb;movsb;movsb;movsb;movsb;add di,bx;movsb;movsb;movsb;movsb;movsb;add di,bx;
				movsb;movsb;movsb;movsb;movsb;add di,bx;movsb;movsb;movsb;movsb;movsb;add di,bx;
				movsb;movsb;movsb;movsb;movsb;add di,bx;movsb;movsb;movsb;movsb;movsb;add di,bx;
				movsb;movsb;movsb;movsb;movsb;add di,bx;movsb;movsb;movsb;movsb;movsb;add di,bx;
				movsb;movsb;movsb;movsb;movsb;add di,bx;movsb;movsb;movsb;movsb;movsb;add di,bx;
				movsb;movsb;movsb;movsb;movsb;add di,bx;movsb;movsb;movsb;movsb;movsb;add di,bx;
				movsb;movsb;movsb;movsb;movsb;add di,bx;movsb;movsb;movsb;movsb;movsb;add di,bx;
				movsb;movsb;movsb;movsb;movsb;add di,bx;movsb;movsb;movsb;movsb;movsb;add di,bx;
				movsb;movsb;movsb;movsb;movsb;add di,bx;movsb;movsb;movsb;movsb;movsb;add di,bx;
				movsb;movsb;movsb;movsb;movsb;add di,bx;movsb;movsb;movsb;movsb;movsb;add di,bx;
				movsb;movsb;movsb;movsb;movsb;add di,bx;movsb;movsb;movsb;movsb;movsb;add di,bx;
				movsb;movsb;movsb;movsb;movsb;add di,bx;movsb;movsb;movsb;movsb;movsb;add di,bx;
				movsb;movsb;movsb;movsb;movsb;add di,bx;movsb;movsb;movsb;movsb;movsb;add di,bx;
				movsb;movsb;movsb;movsb;movsb;add di,bx;movsb;movsb;movsb;movsb;movsb;add di,bx;
				movsb;movsb;movsb;movsb;movsb;add di,bx;movsb;movsb;movsb;movsb;movsb;add di,bx;

				pop si; pop di; pop ds;
			}
			}
		}
		if (s->s_delete == 2)s->init = 0;
	}
	LT_VGA_Return_4Planes();

	//GET NEW BKG's
	for (sprite_number = 0; sprite_number < LT_Sprite_Stack; sprite_number++){	
		SPRITE *s = &sprite[LT_Sprite_Stack_Table[sprite_number]];
		int x = s->pos_x;
		int y = s->pos_y;
		int lx = s->last_x;
		int ly = s->last_y+page_y;
		//if (x == lx && y == ly && s->init == 2) continue;
		if (s->mode == 3) continue;
		//ONLY IF IT IS INSIDE THE ACTIVE MAP
		if ((x > SCR_X-30)&&(x < SCR_X+320)&&(y > SCR_Y-29)&&(y < SCR_Y+243)){
			word bkg_data = s->bkg_data;
			word screen_offset1;
			
			//GOT ITEM? REPLACE BKG BEFORE setting the new tile
			if (s->get_item == 1){
				LT_Edit_MapTile(s->tile_x,s->tile_y,s->ntile_item, s->col_item);
				s->get_item = 0;
			}
			
			//Get pos -1
			screen_offset1 = (ly*50)+(lx>>3);
			
			if (s->init){
			//Copy bkg chunk to a reserved VRAM part, before destroying it
			asm{
				push ds; push di; push si;
				
				mov dx,0x03C4; mov ax,0x0F02; out dx,ax  //write all the bitplanes.
				mov dx,0x03CE; mov ax,0x0008; out dx,ax
				
				mov ax,0A000h; mov ds,ax; mov si,screen_offset1;	//ds:si source vram			
				mov es,ax; mov di,bkg_data	//es:di destination
				mov	bx,50-5

				movsb;movsb;movsb;movsb;movsb;add si,bx;movsb;movsb;movsb;movsb;movsb;add si,bx;
				movsb;movsb;movsb;movsb;movsb;add si,bx;movsb;movsb;movsb;movsb;movsb;add si,bx;
				movsb;movsb;movsb;movsb;movsb;add si,bx;movsb;movsb;movsb;movsb;movsb;add si,bx;
				movsb;movsb;movsb;movsb;movsb;add si,bx;movsb;movsb;movsb;movsb;movsb;add si,bx;
				movsb;movsb;movsb;movsb;movsb;add si,bx;movsb;movsb;movsb;movsb;movsb;add si,bx;
				movsb;movsb;movsb;movsb;movsb;add si,bx;movsb;movsb;movsb;movsb;movsb;add si,bx;
				movsb;movsb;movsb;movsb;movsb;add si,bx;movsb;movsb;movsb;movsb;movsb;add si,bx;
				movsb;movsb;movsb;movsb;movsb;add si,bx;movsb;movsb;movsb;movsb;movsb;add si,bx;
				movsb;movsb;movsb;movsb;movsb;add si,bx;movsb;movsb;movsb;movsb;movsb;add si,bx;
				movsb;movsb;movsb;movsb;movsb;add si,bx;movsb;movsb;movsb;movsb;movsb;add si,bx;
				movsb;movsb;movsb;movsb;movsb;add si,bx;movsb;movsb;movsb;movsb;movsb;add si,bx;
				movsb;movsb;movsb;movsb;movsb;add si,bx;movsb;movsb;movsb;movsb;movsb;add si,bx;
				movsb;movsb;movsb;movsb;movsb;add si,bx;movsb;movsb;movsb;movsb;movsb;add si,bx;
				movsb;movsb;movsb;movsb;movsb;add si,bx;movsb;movsb;movsb;movsb;movsb;add si,bx;
				movsb;movsb;movsb;movsb;movsb;add si,bx;movsb;movsb;movsb;movsb;movsb;add si,bx;
				movsb;movsb;movsb;movsb;movsb;add si,bx;movsb;movsb;movsb;movsb;movsb;add si,bx;
				
				pop si;pop di;pop ds
			}
			
			s->last_last_x = s->last_x;
			s->last_last_y = s->last_y;
			}
		}
	}

_debug:
	//set planes used by sprite
	asm mov dx,0x03C4; asm mov al,0x02; asm mov ah,LT_SPRITES_PLANE_MODE; asm out dx,ax;

	//AT LEAST, DRAW SPRITES
	for (sprite_number = 0; sprite_number < LT_Sprite_Stack; sprite_number++){	
		SPRITE *s = &sprite[LT_Sprite_Stack_Table[sprite_number]];
		int x = s->pos_x;
		int y = s->pos_y;

		//DRAW THE SPRITE ONLY IF IT IS INSIDE THE ACTIVE MAP
		if ((x > SCR_X-26)&&(x < SCR_X+314)&&(y > SCR_Y-32)&&(y < SCR_Y+240)){
			//animation
			if (s->animate == 1){
				s->frame = s->animations[s->anim_counter];
				if (s->anim_speed > s->speed){
					s->anim_speed = 0;
					s->anim_counter++;
					if (s->anim_counter == s->baseframe+8) s->anim_counter = s->baseframe;
				}
				s->anim_speed++;
			}
			//draw sprite and destroy bkg
			if (s->s_delete != 2){
				run_compiled_sprite(((y+page_y)*50)+(x>>3),x&7,s->frames[s->frame]);
			} else s->s_delete = 0;
			s->last_x = x;
			s->last_y = y;
			s->s_delete = 1;
			if (s->init != 2) s->init++;
		} else {
			if (s->s_delete == 1) {
				s->s_delete = 2;
			}
		}
		if (sprite_number){//remove from stack
			if ((x < (SCR_X-96))||(x > SCR_X+320+96)){
				int spr = LT_Sprite_Stack_Table[sprite_number];
				LT_Active_AI_Sprites[s->fixed_sprite_number] = spr; 
				if (LT_Sprite_Stack>1) LT_Sprite_Stack--;
				_memcpy(&LT_Sprite_Stack_Table[sprite_number],&LT_Sprite_Stack_Table[sprite_number+1],8);
				sprite_number--;
				sprite_id_table[s->id_pos] = 0;
				s->id_pos = 0;
			}
		}
		s->last_x = x;
		s->last_y = y;
	}
	//set planes to all
	asm mov dx,0x03C4; asm mov ax,0x0F02; asm out dx,ax;
}

void LT_Draw_Sprites_2(){
	int sprite_number;
	int page_y = pageflip[gfx_draw_page&1];

	LT_VGA_Enable_4Planes();

	//debug green
	//asm mov dx,0x03C8; asm mov al,0; asm out dx,al;
	//asm inc dx;
	//asm mov al,0; asm out dx,al; asm mov al,0xFF; asm out dx,al; asm xor al,al; asm out dx,al;

	//RESTORE SPRITE BKG's
	for (sprite_number = 0; sprite_number < LT_Sprite_Stack; sprite_number++){
		SPRITE *s = &sprite[LT_Sprite_Stack_Table[sprite_number]];
		int x = s->pos_x;
		int y = s->pos_y;
		int lx = s->last_last_x;
		int ly = s->last_last_y+page_y;

		if (s->mode == 3) continue;
		//ONLY IF IT IS INSIDE THE ACTIVE MAP
		if ((x > SCR_X-30)&&(x < SCR_X+320)&&(y > SCR_Y-30)&&(y < SCR_Y+240)){
			if (s->init == 2){
				//Paste at pos -2
				unsigned short screen_offset = (((ly>>4)<<4)*50)+((lx>>4)<<1);
				unsigned short *mapdata =  LT_map_data;
				unsigned short lwidth = LT_VRAM_Logical_Width-2;
				unsigned short map_offset = 0;
				if(gfx_draw_page&1) ly-=pageflip[1];//map y offset
				map_offset = (((ly>>4)<<9)+(lx>>4))<<1;
				asm{
					pusha; push ds;

					mov ax,0A000h; mov ds,ax;

					les bx,[mapdata]
					add bx,map_offset
					mov si,es:[bx]				//ds:si Tile data VRAM address
					mov di,screen_offset		//es:di screen address
				}
				//DRAW TILES
				asm{
					// read ALL locals/params first, before BP is touched
					mov  ax,map_offset
					les  bx,[mapdata]
					mov  dx,es            // dx = map segment
					add  bx,ax            // bx = offset of the current tile entry
					mov  ax,lwidth
					push bp
					mov  bp,bx            // bp = running map pointer
					mov  bx,ax            // bx = lwidth
					mov ax,0xA000; mov es,ax; mov ds,ax // es, ds = VRAM
					mov cx,3 //3 columns
				}

				blit_ega_tile:
				asm{//COPY 3 TILES
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;

					//SET ADDRESS FOR NEXT TILE
					add bp,1024; mov ds,dx; mov si,ds:[bp]; mov ds,ax
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;

					//SET ADDRESS FOR NEXT TILE
					add bp,1024; mov ds,dx; mov si,ds:[bp]; mov ds,ax
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; add di,bx;
					movsb; movsb; add di,bx; movsb; movsb; //sub di below

					//SET ADDRESS AND SCREEN POS FOR NEXT TILE COLUMN
					sub bp,2046; mov ds,dx; mov si,ds:[bp]; mov ds,ax;
					sub di,47*50;

					dec cx; jz end_vga_column; jmp blit_ega_tile;
				}

				end_vga_column:
				asm pop bp;
				asm pop ds; asm popa;
			}
		}
		if (s->s_delete == 2)s->init = 0;
		//Get pos -1
		if (s->init){
			s->last_last_x = s->last_x;
			s->last_last_y = s->last_y;
		}
	}
	LT_VGA_Return_4Planes();

	//debug blue
	//asm mov dx,0x03C8; asm mov al,0; asm out dx,al;
	//asm inc dx;
	//asm mov al,0; asm out dx,al; asm out dx,al; asm mov al,0xFF; asm out dx,al;

	//set planes used by sprite
	asm mov dx,0x03C4; asm mov al,0x02; asm mov ah,LT_SPRITES_PLANE_MODE; asm out dx,ax;

	//AT LEAST, DRAW SPRITES
	for (sprite_number = 0; sprite_number < LT_Sprite_Stack; sprite_number++){
		SPRITE *s = &sprite[LT_Sprite_Stack_Table[sprite_number]];
		int x = s->pos_x;
		int y = s->pos_y;

		//GOT ITEM? REPLACE BKG
		if (s->get_item == 1){
			LT_Edit_MapTile(s->tile_x,s->tile_y,s->ntile_item, s->col_item);
			s->get_item = 0;
		}
		//DRAW THE SPRITE ONLY IF IT IS INSIDE THE ACTIVE MAP
		if ((x > SCR_X-26)&&(x < SCR_X+314)&&(y > SCR_Y-24)&&(y < SCR_Y+234)){
			//animation
			if (s->animate == 1){
				s->last_frame = s->frame;
				if (s->anim_speed > s->speed){
					s->anim_speed = 0;
					s->anim_counter++;
					if (s->anim_counter == s->baseframe+8) s->anim_counter = s->baseframe;
				}
				s->anim_speed++;
				s->frame = s->animations[s->anim_counter];
			}
			//draw sprite and destroy bkg
			if (s->s_delete != 2){
				run_compiled_sprite(((y+page_y)*50)+(x>>3),x&7,s->frames[s->frame]);
			} else s->s_delete = 0;
			s->last_x = x;
			s->last_y = y;
			s->s_delete = 1;
			if (s->init != 2) s->init++;
		} else {
			if (s->s_delete == 1) {
				s->s_delete = 2;
			}
		}
		if (sprite_number){//remove from stack
			if ((x < (SCR_X-96))||(x > SCR_X+320+96)){
				int spr = LT_Sprite_Stack_Table[sprite_number];
				LT_Active_AI_Sprites[s->fixed_sprite_number] = spr;
				if (LT_Sprite_Stack>1) LT_Sprite_Stack--;
				_memcpy(&LT_Sprite_Stack_Table[sprite_number],&LT_Sprite_Stack_Table[sprite_number+1],8);
				sprite_number--;
				sprite_id_table[s->id_pos] = 0;
				s->id_pos = 0;
			}
		}
		s->last_x = x;
		s->last_y = y;
	}
	//set planes to all
	asm mov dx,0x03C4; asm mov ax,0x0F02; asm out dx,ax;
}

void LT_Set_AI_Sprite(byte sprite_number, byte mode, word x, word y, int sx, int sy, word id_pos){
	//int i = 0;
	SPRITE *s = &sprite[sprite_number];
	s->pos_x = x<<4;
	s->pos_y = y<<4;
	s->mspeed_x = sx;
	s->mspeed_y = sy;
	if (s->mspeed_x!=0) {s->speed_x = 3; s->speed_y = 0;}
	if (s->mspeed_y!=0) {s->speed_x = 0; s->speed_y = 3;}
	s->mode = mode;
	s->ai_stack = LT_Sprite_Stack;
	s->id_pos = id_pos;
	s->init = 0;
	s->s_delete = 0;
}


void LT_Set_AI_Sprites(byte mode_0, byte mode_1){
	int i;

	LT_AI_Enabled = 1;

	if (mode_0 > 3) mode_0 = 0;
	if (mode_1 > 3) mode_1 = 0;

	if (mode_0 == mode_1){
		for (i = 0; i < 6; i++) {
			LT_Clone_Sprite(1+i,1);
			sprite[1+i].fixed_sprite_number = i;
			sprite[1+i].mode = mode_0;
			LT_Active_AI_Sprites[i] = 1+i;
		}
	}
	else {
		for (i = 0; i < 3; i++) {
			LT_Clone_Sprite(1+i,1);
			sprite[1+i].fixed_sprite_number = i;
			sprite[1+i].mode = mode_0;
			LT_Active_AI_Sprites[i] = 1+i;

			LT_Clone_Sprite(4+i,4);
			sprite[4+i].fixed_sprite_number = 3+i;
			sprite[4+i].mode = mode_1;
			LT_Active_AI_Sprites[3+i] = 4+i;
		}
	}
}

void LT_move_player(int sprite_number){
	SPRITE *s = &sprite[sprite_number];
	int tsize = 4;//16
	byte tilememsize = 0;
	byte col_x = 0;
	byte col_y = 0;
	int x,y,platform_y;
	byte size = 32;
	byte siz = 32 -1;
	byte si = (32/2)+1;
	int pos_x = s->pos_x;
	int pos_y = s->pos_y;
	int tile_x = 0;
	/*
	#define SPR_TILP_NUM 0;	
	#define SPR_TILC_NUM 1;	
	#define SPR_COL_X	 2;
	#define SPR_COL_Y	 3;
	#define SPR_HIT		 4;
	#define SPR_JUMP	 5;
	#define SPR_ACT		 6;
	#define SPR_MOVE	 7;	//0 1 2 3 4 => Stop U D L R 
	#define SPR_GND		 8;
	*/

	int last_dir;
	//GET TILE POS
	tilememsize = 5;
	s->tile_x = (pos_x+si)>>tsize;
	s->tile_y = (pos_y+si)>>tsize;
	tile_x = s->tile_x;
	tile_number = (LT_map_data[(s->tile_y << 9) + s->tile_x]-TILE_VRAM)>>tilememsize;
	tilecol_number = LT_map_collision[(((pos_y+si)>>tsize) <<9) + ((pos_x+si)>>tsize)];
	tilecol_number_down = LT_map_collision[(((pos_y+size+1)>>tsize) <<9) + ((pos_x+si)>>tsize)];
	
	LT_Player_State[SPR_MOVE] = 0;
	LT_Player_State[SPR_GND] = 0;
	s->state = 0;
	//PREDEFINED GAME TYPES
	switch (LT_MODE){
	case 0:{//TOP
		LT_Player_State[SPR_GND] = s->ground = 1;
		if (LT_Keys[LT_UP]){	//UP
			LT_Player_State[SPR_MOVE] = 1;
			LT_Player_Dir = 0;
			col_y = 0;
			y = (pos_y+4)>>tsize;
			tile_number_VR = LT_map_collision[( y <<9) + ((pos_x+siz-6)>>tsize)];
			tile_number_VL = LT_map_collision[( y <<9) + ((pos_x+6)>>tsize)];
			if ((tile_number_VR == 1) || (tile_number_VR == 2)) col_y = tile_number_VR;
			if ((tile_number_VL == 1) || (tile_number_VL == 2)) col_y = tile_number_VL;
			if (col_y == 0) s->pos_y--;
		} else if (LT_Keys[LT_DOWN]){//DOWN
			LT_Player_State[SPR_MOVE] = 2;
			LT_Player_Dir = 1;
			col_y = 0;
			y = (pos_y+size-4)>>tsize;
			tile_number_VR = LT_map_collision[( y <<9) + ((pos_x+siz-6)>>tsize)];
			tile_number_VL = LT_map_collision[( y <<9) + ((pos_x+6)>>tsize)];
			if ((tile_number_VR == 1) || (tile_number_VR == 2)) col_y = tile_number_VR;
			if ((tile_number_VL == 1) || (tile_number_VL == 2)) col_y = tile_number_VL;
			if (col_y == 0) s->pos_y++;
		}
		
		if (LT_Keys[LT_LEFT]){	//LEFT
			LT_Player_State[SPR_MOVE] = 3;
			LT_Player_Dir = 2;
			s->state = 3;
			col_x = 0;
			x = (pos_x+4)>>tsize;
			tile_number_HU = LT_map_collision[(((pos_y+6)>>tsize) <<9) + x];
			tile_number_HD = LT_map_collision[(((pos_y+siz-4)>>tsize) <<9) + x];
			if ((tile_number_HU == 1) || (tile_number_HU == 2)) col_x = tile_number_HU;
			if ((tile_number_HD == 1) || (tile_number_HD == 2)) col_x = tile_number_HD;
			if (col_x == 0) s->pos_x--;
		} else if (LT_Keys[LT_RIGHT]){	//RIGHT
			LT_Player_State[SPR_MOVE] = 4;
			LT_Player_Dir = 3;
			s->state = 4;
			col_x = 0;
			x = (pos_x+size-4)>>tsize;
			tile_number_HU = LT_map_collision[(((pos_y+6)>>tsize) <<9) + x];
			tile_number_HD = LT_map_collision[(((pos_y+siz-4)>>tsize) <<9) + x];
			if ((tile_number_HU == 1) || (tile_number_HU == 2)) col_x = tile_number_HU;
			if ((tile_number_HD == 1) || (tile_number_HD == 2)) col_x = tile_number_HD;
			if (col_x == 0) s->pos_x++;
		}
	break;}
	case 1:{//PLATFORM
		if ((s->ground == 1) && (LT_Jump_Release) && (LT_Keys[LT_JUMP])) {s->ground = 0; s->jump_frame = 0; s->jump = 1;}
		if (s->jump == 1){//JUMP
			col_y = 0;
			s->ground = 0;
			s->climb = 0;
			LT_Jump_Release = 0;
			if (s->jump_frame < 39){//RISING
				// check the position the player is about to move to, since speed is up to 6 px/frame
				y = (pos_y + LT_player_jump_pos[s->jump_frame] + 3)>>tsize;
				tile_number_VR = LT_map_collision[( y <<9) + ((pos_x+siz-6)>>tsize)];
				tile_number_VL = LT_map_collision[( y <<9) + ((pos_x+6)>>tsize)];
				if (tile_number_VR == 1) col_y = 1;
				if (tile_number_VR == 2) col_y = 2;
				if (tile_number_VL == 1) col_y = 1;
				if (tile_number_VL == 2) col_y = 2;
				if (col_y > 0) s->jump_frame = 40;   // bumped head: start falling slowly
				//Short jumps: releasing the key while still rising skips ahead to the slow part
				if (!LT_Keys[LT_JUMP]){
					if ((s->jump_frame >= 10) && (s->jump_frame < 28)) s->jump_frame = 28;
					LT_Jump_Release = 1;
				}
			} else {//FALLING
				int platform_y = 4+(((pos_y+size)>>tsize)<<4);
				y = (pos_y+size)>>tsize;
				tile_number_VR = LT_map_collision[( y <<9) + ((pos_x+siz-6)>>tsize)];
				tile_number_VL = LT_map_collision[( y <<9) + ((pos_x+6)>>tsize)];
				if (tile_number_VR > 0 && tile_number_VR < 6) col_y = 1;
				if (tile_number_VL > 0 && tile_number_VL < 6) col_y = 1;
				if ((col_y == 1)&&(pos_y+size > platform_y)) col_y = 0;
				if (col_y > 0) s->jump_frame = 78;
			}
			if (s->jump_frame < 77) {
				s->pos_y += LT_player_jump_pos[s->jump_frame];
				s->jump_frame++;
			} else {
				if (col_y == 0) {s->ground = 0; s->pos_y+=3;}   // terminal fall speed
				if (col_y > 0) {
					s->ground = 1;
					if (pos_y&15)s->pos_y--;
					else {s->jump = 2; s->jump_frame = 0;}
				}
			}
		}
		
		if(LT_Keys[LT_UP]){//CLIMB UP, stuck if press L R at the top of a ladder
			if ((tilecol_number >= 4 && tilecol_number <= 7) ||
				(tilecol_number_down >= 4 && tilecol_number_down <= 5))
			{
				//int xl,xr;
				LT_Player_Dir = 0;
				LT_Player_State[SPR_MOVE] = 1;
				s->ground = 1;
				s->jump = 2;
				s->climb = 1;
				col_y = 0;
				y = ((pos_y-1)>>tsize)<<9;
				x = (pos_x+6)>>tsize;
				tile_number_VR = LT_map_collision[y + ((pos_x+siz-6)>>tsize)];
				tile_number_VL = LT_map_collision[y + x];
				if (tile_number_VR == 1 || tile_number_VR == 2) col_y = 1;
				if (tile_number_VL == 1 || tile_number_VL == 2) col_y = 1;

				if (tilecol_number_down == 4 || tilecol_number_down == 6 ) s->pos_x = (tile_x)<<tsize;
				if (tilecol_number_down == 5 || tilecol_number_down == 7 ) s->pos_x = (tile_x-1)<<tsize;

				if (col_y == 0) s->pos_y--;
			}
		} else if(LT_Keys[LT_DOWN]){//CLIMB DOWN
			if ((tilecol_number >= 4 && tilecol_number <= 7) ||
				(tilecol_number_down >= 4 && tilecol_number_down <= 5))
			{
				LT_Player_Dir = 1;
				LT_Player_State[SPR_MOVE] = 1;
				s->jump = 2;
				s->ground = 1;
				s->climb = 1;
				col_y = 0;
				y = (((pos_y+size)>>tsize)<<9);
				tile_number_VR = LT_map_collision[y + ((pos_x+siz-6)>>tsize)];
				tile_number_VL = LT_map_collision[y + ((pos_x+6)>>tsize)];
				if (tile_number_VR >= 1 && tile_number_VR <= 3) col_y = 1;
				if (tile_number_VL >= 1 && tile_number_VR <= 3) col_y = 1;

				if (tilecol_number_down == 4 || tilecol_number_down == 6 ) s->pos_x = (tile_x)<<tsize;
				if (tilecol_number_down == 5 || tilecol_number_down == 7 ) s->pos_x = (tile_x-1)<<tsize;

				if (col_y == 0) s->pos_y++;
				else s->climb = 0;
			}
		}
		if (s->jump == 2){
			// Disable climb and set the start of the fall phase
			if ((tilecol_number != 6) && (tilecol_number != 7) &&
				(tilecol_number_down != 4) && (tilecol_number_down != 5) &&
				(tilecol_number_down != 6) && (tilecol_number_down != 7))
			{
				s->climb = 0;
				s->jump = 1;
				s->jump_frame = 40;
			}
		}
		if (!LT_Keys[LT_JUMP] && s->ground) LT_Jump_Release = 1;
		if (LT_Keys[LT_LEFT]){	//LEFT
			LT_Player_Dir = 2;
			if (s->ground) LT_Player_State[SPR_MOVE] = 3;
			if (s->climb) {LT_Player_State[SPR_MOVE] = 1; LT_Player_Dir = 0;}
			s->state = 3;
			col_x = 0;
			x = (pos_x+1)>>tsize;
			tile_number_HU = LT_map_collision[((pos_y>>tsize) <<9) + x];
			tile_number_HD = LT_map_collision[(((pos_y+siz)>>tsize) <<9) + x];
			if ((tile_number_HU == 1) || (tile_number_HU == 2)) col_x = tile_number_HU;
			if ((tile_number_HD == 1) || (tile_number_HD == 2)) col_x = tile_number_HD;
			if (col_x == 0) s->pos_x--;
		} else if (LT_Keys[LT_RIGHT]){	//RIGHT
			LT_Player_Dir = 3;
			if (s->ground) LT_Player_State[SPR_MOVE] = 4;
			if (s->climb) {LT_Player_State[SPR_MOVE] = 1; LT_Player_Dir = 0;}
			s->state = 4;
			col_x = 0;
			x = (pos_x+size-2)>>tsize;
			tile_number_HU = LT_map_collision[((pos_y>>tsize) <<9) + x];
			tile_number_HD = LT_map_collision[(((pos_y+siz)>>tsize) <<9) + x];
			if ((tile_number_HU == 1) || (tile_number_HU == 2)) col_x = tile_number_HU;
			if ((tile_number_HD == 1) || (tile_number_HD == 2)) col_x = tile_number_HD;
			if (col_x == 0) s->pos_x++;
		} 
		LT_Player_State[SPR_GND] = s->ground;
	
	break;}	
	};
	
	if (s->jump_frame == 1) LT_Player_State[SPR_JUMP] = 1;
	else LT_Player_State[SPR_JUMP] = 0;
	LT_Player_State[SPR_TILP_NUM] = tile_number;
	LT_Player_State[SPR_TILC_NUM] = tilecol_number;
	LT_Player_State[SPR_COL_X] = col_x;
	LT_Player_State[SPR_COL_Y] = col_y;
	
	//Fixed animations
	if (LT_Player_State[SPR_GND]){
		switch (LT_Player_State[SPR_MOVE]){
			case LT_SPR_UP: LT_Set_Sprite_Animation(sprite_number,0); break;
			case LT_SPR_DOWN: LT_Set_Sprite_Animation(sprite_number,1); break;
			case LT_SPR_LEFT: LT_Set_Sprite_Animation(sprite_number,2); break;
			case LT_SPR_RIGHT: LT_Set_Sprite_Animation(sprite_number,3); break;
			case LT_SPR_STOP:
				if (LT_Player_Dir == 0) s->frame = s->animations[ 0];
				if (LT_Player_Dir == 1) {
					if (!s->climb) s->frame = s->animations[ 8];
					else s->frame = s->animations[ 0];
				}
				if (LT_Player_Dir == 2) s->frame = s->animations[16];
				if (LT_Player_Dir == 3) s->frame = s->animations[24];
				LT_Sprite_Stop_Animation(sprite_number);
			break;
		}
	} else {
		switch (LT_Player_Dir){
			case 2: LT_Set_Sprite_Animation(sprite_number,4); break;
			case 3: LT_Set_Sprite_Animation(sprite_number,5); break;
		}
	}
}

	
void LT_Update_AI_Sprites(){
	int i = 0;
	int motion = 0;
	int SCX = SCR_X;
	if (LT_AI_Enabled){
	//for (sprite_number = LT_Sprite_Stack; sprite_number > 1; sprite_number--){
	for (i = 1; i < LT_Sprite_Stack; i++){
		SPRITE *s = &sprite[LT_Sprite_Stack_Table[i]];
		int px = s->pos_x;
		int py = s->pos_y;
		//CALCULATE ONLY IF IT IS INSIDE THE ACTIVE MAP
		if ((px > SCX-96)&&(px < (SCX+320+96))&&(py > SCR_Y-96)&&(py < (SCR_Y+340+96))){
			byte col_x = 0;
			byte col_y = 0;
			int x,y;
			int sx = s->mspeed_x;
			int sy = s->mspeed_y;
			byte siz = 33;
			byte si = (32/2) +1;
			
			switch (s->mode){
			case 0:{ //WALKS ON TOP VIEW
				y = (py+si>>4);
				s->tile_y = y;
				switch (sx){
					case -1:	//LEFT
					motion = 3;
					col_x = 0;
					x = (px-1)>>4;
					tile_number_HU = LT_map_collision[( y <<9) + x];
					if (tile_number_HU == 1) col_x = 1;
					if (tile_number_HU == 2) col_x = 1;
					if (col_x == 1) {s->speed_x = 0; s->speed_y = 3; s->mspeed_x = 0;s->mspeed_y = -1;}
					break;
					case 1:		//RIGHT
					motion = 4;
					col_x = 0;
					x = (px+siz)>>4;
					tile_number_HU = LT_map_collision[( y <<9) + x];
					if (tile_number_HU == 1) col_x = 1;
					if (tile_number_HU == 2) col_x = 1;
					if (col_x == 1) {s->speed_x = 0; s->speed_y = 3; s->mspeed_x = 0; s->mspeed_y = 1;}
					break;
				}
				x = (px+si)>>4;
				s->tile_x = x;
				switch (sy){
					case -1:	//UP
					motion = 1;
					col_y = 0;
					y = (py-1)>>4;
					tile_number_VR = LT_map_collision[( y <<9) + x];
					if (tile_number_VR == 1) col_y = 1;
					if (tile_number_VR == 2) col_y = 1;
					if (col_y == 1) {s->speed_x = 3; s->speed_y = 0;s->mspeed_y = 0; s->mspeed_x = 1;}
					break;
					case 1:		//DOWN
					motion = 2;
					col_y = 0;
					y = (py+siz)>>4;
					tile_number_VR = LT_map_collision[( y <<9) + x];
					if (tile_number_VR == 1) col_y = 1;
					if (tile_number_VR == 2) col_y = 1;
					if (col_y == 1) {s->speed_x = 3; s->speed_y = 0;s->mspeed_y = 0; s->mspeed_x = -1;}
					break;
				}
			break;}
			case 1:{ //ONLY WALKS ON PLATFORMS UNTILL IT REACHES EDGES OR SOLID TILES
				switch (sx){
					case -1:	//LEFT
					motion = 3;
					col_x = 0;
					x = (px-1)>>4;
					tile_number_HU = LT_map_collision[(((py+si)>>4) <<9) + x];
					tile_number_HD = LT_map_collision[(((py+siz)>>4) <<9) + x];
					if (tile_number_HU == 1) col_x = 1;
					if (tile_number_HU == 2) col_x = 1;
					if (tile_number_HD == 8) col_x = 1;  //Platform edges
					if (tile_number_HD == 9) col_x = 1;  //Platform edges
					if (col_x == 1) s->mspeed_x *= -1;
					break;
					case 1:		//RIGHT
					motion = 4;
					col_x = 0;
					x = (px+siz)>>4;
					tile_number_HU = LT_map_collision[(((py+si>>4)) <<9) + x];
					tile_number_HD = LT_map_collision[(((py+siz)>>4) <<9) + x];
					if (tile_number_HU == 1) col_x = 1;
					if (tile_number_HU == 2) col_x = 1;
					if (tile_number_HD == 8) col_x = 1;  //Platform edges
					if (tile_number_HD == 9) col_x = 1;  //Platform edges
					if (col_x == 1) s->mspeed_x *= -1;
					break;
				}
			break;}
			case 2:{ //Only flies to the left
				s->speed_x = 1;
				s->mspeed_x = -1;
			break;}
			case 3:{ //Only flies to the right
				s->speed_x = 1;
				s->mspeed_x = 1;
				break;}
			}
			col_x = 0;
			col_y = 0;
			
			switch (motion){
				case 1: LT_Set_Sprite_Animation(LT_Sprite_Stack_Table[i],0); break;
				case 2: LT_Set_Sprite_Animation(LT_Sprite_Stack_Table[i],1); break;
				case 3: LT_Set_Sprite_Animation(LT_Sprite_Stack_Table[i],2); break;
				case 4: LT_Set_Sprite_Animation(LT_Sprite_Stack_Table[i],3); break;
			}
			//SPEED CONTROL TO MAKE ENEMIES TO MOVE SLOWER THAN +=1
			if (s->speed_x != 0){
				if (s->s_x == s->speed_x) {
					s->s_x = 0;
					s->pos_x += s->mspeed_x;
				}
				s->s_x ++;
			}
			if (s->speed_y != 0){
				if (s->s_y == s->speed_y) {
					s->s_y = 0;
					s->pos_y += s->mspeed_y;
				}
				s->s_y ++;
			}
		}
	}
	}
}

int LT_Player_Col_Enemy(){
	unsigned char i = 0;
	int col = 0;
	int ex = 0;
	int ey = 0;
	SPRITE *p = &sprite[LT_Sprite_Stack_Table[0]];
	int px = p->pos_x;
	int py = p->pos_y;
	for (i = 1; i < LT_Sprite_Stack; i++){
		SPRITE *s = &sprite[LT_Sprite_Stack_Table[i]];
		ex = s->pos_x;
		ey = s->pos_y;
		if ((_abs(px-ex)<LT_ENEMY_DIST_X)&&(_abs(py-ey)<LT_ENEMY_DIST_Y)) col = 1;
	}
	return col;
}

void LT_Unload_Sprites(){
	int i = 0;int f = 0;
	for (i=0;i<20;i++){
		SPRITE *s = &sprite[i];
		s->init = 0;
		s->code_size = 0;
		for (f=0;f<64;f++){
			s->frames[f] = LT_Sprite_Empty_Frame;
		}
	}
	LT_AI_Enabled = 0;
	LT_sprite_data_offset = 0;
	LT_memset(LT_sprite_data,0,0x65535);
	//LT_memset(LT_sprite_data1,0,0x65535);
}	
