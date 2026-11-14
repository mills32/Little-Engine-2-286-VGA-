#include "lt__eng.h"
#include <dos.h> //NEEDED for FP_OFF, FP_SEG, MK_FP

#define MISC_OUTPUT         0x03c2    // VGA misc. output register
#define S_MEMORY_MODE		0x03C4	
#define INPUT_STATUS_0		0x03da
#define AC_MODE_CONTROL		0x10	  //Index of Mode COntrol register in AC
#define AC_INDEX			0x03c0	  //Attribute controller index register
#define CRTC_INDEX			0x03d4	  // CRT Controller Index
  
#define SC_INDEX            0x03c4    // VGA sequence controller
#define SC_DATA             0x03c5

#define GC_INDEX            0x03ce    // VGA graphics controller
#define GC_DATA             0x03cf

#define MAP_MASK            0x02
#define ALL_PLANES          0xff02
 
 
//debugging
float t1 = 0;
float t2 = 0;

//timer     		
long time_ctr;

// this points to the 18.2hz system clock for debug in dosbox (does not work on PCEM). 
word *my_clock=(word *)0x0000046C; 
   		
word start;
int Free_RAM;
byte LT_SCROLL_MODE = 0;//0 original; 1 alternative; 2 for fast PC
byte LT_DETECTED_CARD = 0;
byte LT_MUSIC_MODE = 0;//1 ADLIB; 2 MOD SB
byte LT_MUSIC_ENABLE = 0;// disable-enable
byte LT_SFX_MODE = 0;
byte LT_LANGUAGE = 0;
byte LT_DEBUG = 0;
byte LT_MODMASTER_FREQ = 1;
byte LT_ENDLESS_SIDESCROLL = 0;
byte LT_IMAGE_MODE = 0;	//0 map; 1 image
byte LT_SPRITE_MODE = 0; //0 fast, no delete; 1 slow, delete

extern byte *VGA;
extern byte *CGA;
extern word TILE_VRAM;
extern word FONT_VRAM;
word LT_setupfile;
extern byte far *LT_music_sdata;

// define LT_old_..._handler to be a pointer to a function
void interrupt (*LT_old_time_handler)();
void LT_vsync();

void LT_WaitVsync_VGA();
void CalibrateMaxHblankLength();
void LT_WaitVsync_VGA_Compatible();
void LT_WaitVsync_VGA_Debug();
void LT_WaitVsync_VGA_crappy();
void LT_Scroll_Map_GFX();
extern void (*LT_Load_Music)(char*,char*);
extern void (*LT_Play_Music)(void);
extern void (*LT_Start_Music)(void);
extern void (*LT_Stop_Music)(void);
extern void (*LT_Draw_Sprites)(void);
void Load_Music_Adlib(char *fname, char *dat_string);
void LT_Start_VGM();
void Play_Music_VGM();
void LT_Stop_VGM();
void LT_Draw_Sprites_1();
void LT_Draw_Sprites_2();
void LT_MODMaster_Init(int rate);
void LT_MOD_Load(char *file, char *dat_string);
void LT_Stop_MOD();
void LT_mod_update();
void LT_Start_MOD();

//Physical width in bytes of screen
extern word LT_VRAM_Logical_Width;	
extern byte sprite_size;
extern LT_sprite_data_offset;
extern unsigned char far *LT_sprite_data; 
extern byte far *sprite_id_table;
extern word far *LT_map_data;
extern byte far *LT_map_collision;
void Check_Graphics_Card();

extern void LT_vsync();
extern void (*LT_WaitVsync)();
byte LT_Wait_kbhit();
unsigned char far *LT_data;
extern byte LT_Sprite_Empty_Frame[];

void LT_Null(){//Debug
	int i = 0;
	i = i+1;
}

byte far LT_setup_data[] = {"\
#SETUP---------\n\
VIDEO=0        \n\
SCROLL=0       \n\
MUSIC=3        \n\
SFX=0          \n\
JOYX=AABB      \n\
JOYY=AABB0     \n\
LANG=0         \n\
#SAVE----------\n\
LEV_A=0LIV_A=0 \n\
LEV_B=0LIV_B=0 $"
};

extern unsigned char LT_JOYSTICK;
extern unsigned short LT_JOY0_DEAD_X0;
extern unsigned short LT_JOY0_DEAD_X1;
extern unsigned short LT_JOY0_DEAD_Y0;
extern unsigned short LT_JOY0_DEAD_Y1;

word sprite_bkg[8] = {
	0xE4E8,0xE588,0xE628,0xE6C8,0xE768,0xE808,0xE8A8,0xE948,
};


/////////////////////////////////////////////////////
//I defined my own functions to avoid using any libs
//Most of them are bios/dos functions
/////////////////////////////////////////////////////

void _printf(byte *text){
	asm mov ah,0x09
	asm lds dx,text
	asm int 21h
}

void LT_Text_Mode(){
	if (LT_DETECTED_CARD == 3){
		outportb(0x3D4, 0x0A);
		outportb(0x3D5, (inportb(0x3D5) & 0xC0) | 14);
		outportb(0x3D4, 0x0B);
		outportb(0x3D5, (inportb(0x3D5) & 0xE0) | 15);
	}
	asm mov ax,0x0003
	asm int 0x10
}

void LT_Exit(){
	asm mov	ax,0x4C00
	asm int	0x21
}

void LT_memset(void *ptr, byte val, word number){
	void *data = ptr;
	asm push es
	asm push di
	asm mov cx,number
	asm les di,data
	asm mov al,val
	asm rep stosb	//ax to es:di
	asm pop di
	asm pop es
}

void LT_Error(char *error, char *file);

void LT_free(void far* ptr) {
    if (ptr) {
		word segment = FP_SEG(ptr);
		asm mov ax,segment
		asm mov es,ax
		asm mov ax,0x4900
		asm int 21h
		asm jc _error:
		/// in both Microsoft C and Turbo C, far* returned in DX:AX
		return;
		_error:
		return;
	}
}

void far *LT_calloc0(word para){
	word addr = 0;
    asm mov ah,48h
    asm mov bx,para
    asm int 21h
    asm jc    _error
    asm mov addr,ax
    return (void far *) MK_FP(addr,0);
    _error:
	//if AX = 8, BX = free paragraphs
    return 0;
}

void LT_setvect(byte intr, void interrupt (far *func)());
byte LT_strlen(char *st1);
void LT_Error(char *error, char *file){
	byte len = LT_strlen(file);
	file[len] = '$';
	LT_reset_key_handler();
	LT_Stop_Music();
	LT_Disable_Speaker();
	//LT_Fade_out();
	LT_Text_Mode();
	//system("mem");
	LT_free(LT_data);
	_printf(error);
	_printf(file);

	asm CLI
	//reset time handler
	asm mov al,0x36
	asm out 0x43,al
	asm mov al,0xFF
	asm out 0x40,al
	asm out 0x40,al
	LT_setvect(0x1C, LT_old_time_handler);
	asm STI

	LT_sleep(2);
	LT_Exit();
}

word LT_fcreate(unsigned char *file){
	unsigned char *path = file;
	word file_handle = 0;
	asm push ds
	asm mov ah,3Ch		//create file
	asm mov cx,0x04 	//0000 0100 = archive
	asm lds dx,path		//filename to create
	asm int 21h
	asm jc _error
	asm mov file_handle,ax
	return file_handle;
	_error:
	return 0;
}

word LT_fopen__(unsigned char *file, byte mode){
	word file_handle = 0;
	asm push ds
	asm mov ah,3Dh		//open file
	asm mov al,mode 	//read only 00, w 01, rw 02
	asm lds dx,file		//filename to open
	asm int 21h
	asm mov file_handle,ax
	asm pop ds
	asm cmp file_handle,2
	asm jz _no_file
	return file_handle;
	_no_file:
	return 0;
}

word LT_fopen(unsigned char *file, byte mode){
	word file_handle = 0;
	asm push ds
	asm mov ah,3Dh		//open file
	asm mov al,mode		//read only 00, w 01, rw 02
	asm lds dx,file		//filename to open
	asm int 21h
	asm jc _open_fail
	asm mov file_handle,ax
	asm pop ds
	return file_handle;
	_open_fail:
	asm pop ds
	return 0;
}

void LT_fread(word file_handle,word bytes,void *buffer){
	asm push ds
	asm mov ah,3Fh
	asm mov bx,file_handle
	asm mov cx,bytes
	asm mov dx,word ptr buffer
	asm mov ds,word ptr [buffer+2]
	asm int 21h
	asm pop ds
}

void LT_fwrite(word file_handle,word bytes,unsigned char *buffer){
	unsigned char *write_buffer = buffer;
	asm push ds
	asm mov ah,40h
	asm mov cx,bytes
	asm mov bx,file_handle
	asm lds dx,write_buffer
	asm int 21h
	asm pop ds
}

unsigned long LT_fseek(word file_handle,unsigned long bytes,byte origin){
	unsigned long file_pos = 0;
	asm mov ah,42h
	asm mov al,origin //(0,1,2 SEEK_SET SEEK_CUR SEEK_END)
	asm mov bx,file_handle
	asm mov cx,word ptr [bytes+2]
	asm mov dx,word ptr bytes
	asm int 21h
	asm jc    _error
	asm mov word ptr [file_pos],ax
	asm mov word ptr [file_pos+2],dx//new pointer location if CF not set
	return file_pos;
	_error:
	return 0;
}

void LT_fclose(word file_handle){
	asm mov ah,0x3E
	asm mov bx,file_handle		//filename to close
	asm int 21h
}

byte LT_stricmp(char *st1, char *st2, byte n){
	byte i = 0;
	while(1){
		byte a = st1[i];byte b = st2[i];
		if (i && !a && !b) return 0;
		//to lower case if required
		if ((a>64)&&(a<91)) a+=32;
		if ((b>64)&&(b<91)) b+=32;
		//compare
		if (a != b) return 1;
		if (i == n-1) return 0;
		i++;
	}
	//return 1;
}

byte LT_strlen(char *st1){
	byte i = 0;
	while(1){
		byte a = st1[i];
		if(!a) return i;
		i++;
	}
}


void interrupt (far * LT_getvect(byte intr))(){
	word _segment = 0;
	word _offset = 0;
	asm	mov	ah, 0x35
	asm	mov	al, intr
	asm	int	0x21
	asm xchg ax,bx
	asm mov dx,es
	asm mov _segment,dx
	asm mov _offset,ax
	return( (void interrupt (*)()) ((unsigned long)MK_FP(_segment,_offset)));
}

void LT_setvect(byte intr, void interrupt (far *func)()){
	word _segment = FP_SEG(func);
	word _offset = FP_OFF(func);
	asm	push	ds
	asm	mov	ah, 0x25
	asm	mov	al, intr
	asm	mov ds,_segment; asm mov dx,_offset	
	asm	int	0x21
	asm	pop	ds
}

void LT_sleep(byte seconds){
	seconds*=60;
	while (seconds){seconds--;PC_Speaker_SFX_Player();LT_vsync();}
}


byte LT_GET_MUSIC(){return LT_MUSIC_MODE;}

/////////////////////////////////////
/////////////////////////////////////

//Seek files inside LDAT files (custom format similar to WAD)

void DAT_Seek2(word fp,char *dat_string){
	unsigned char line = 0;
	char name[17]; //name of the file inside DAT
	unsigned char data_name = 0;
	dword LDAT_Offset = 0;
	
	//Check LDAT file
	//Seek data
	LT_fseek(fp,32,SEEK_SET);
	while(data_name == 0){//read 16 byte lines 
		LT_memset(name, 0, 17);//set all = 0
		LT_fread(fp,16,name);
		if (!LT_stricmp(name,dat_string,15)) data_name = 1;
		else LT_fseek(fp,16,SEEK_CUR);//advance
		line++;
		if (line == 64) {LT_fclose(fp); LT_Error("Can't find file inside DAT file: $",dat_string);}
	}
	LT_fread(fp,sizeof(LDAT_Offset),(byte*)&LDAT_Offset);//read offset of file in DAT file
	LT_fseek(fp,LDAT_Offset,SEEK_SET);
}


void Read_Setup(){
	byte buffer[0xB0];
	LT_fread(LT_setupfile,0xB0,buffer);
	LT_MODMASTER_FREQ = buffer[0x16]-48;
	LT_SCROLL_MODE = buffer[0x27]-48;
	LT_MUSIC_MODE = buffer[0x36]-48;
	LT_SFX_MODE = buffer[0x44]-48;
	LT_JOY0_DEAD_X0 = (unsigned short)buffer[0x55];
	LT_JOY0_DEAD_X1 = (unsigned short)buffer[0x57];
	LT_JOY0_DEAD_Y0 = (unsigned short)buffer[0x65];
	LT_JOY0_DEAD_Y1 = (unsigned short)buffer[0x67];
	LT_JOYSTICK = buffer[0x69]-48;
	LT_LANGUAGE = buffer[0x75]-48;
	
	switch (LT_SCROLL_MODE){
		case 0: LT_WaitVsync = &LT_WaitVsync_VGA; break;
		case 1: LT_WaitVsync = &LT_WaitVsync_VGA_Compatible; break;
		case 2: LT_WaitVsync = &LT_WaitVsync_VGA_crappy; break;
	}
}

void Save_Setup(){
	LT_setup_data[0x16] = LT_MODMASTER_FREQ+48;
	LT_setup_data[0x27] = LT_SCROLL_MODE+48;
	LT_setup_data[0x36] = LT_MUSIC_MODE+48;
	LT_setup_data[0x44] = LT_SFX_MODE+48;
	LT_setup_data[0x55] = LT_JOY0_DEAD_X0;
	LT_setup_data[0x57] = LT_JOY0_DEAD_X1;
	LT_setup_data[0x65] = LT_JOY0_DEAD_Y0;
	LT_setup_data[0x67] = LT_JOY0_DEAD_Y1;
	LT_setup_data[0x69] = LT_JOYSTICK+48;
	LT_setup_data[0x75] = LT_LANGUAGE+48;
	LT_fseek(LT_setupfile,0,0);
	LT_fwrite(LT_setupfile,0xB0,LT_setup_data);
}


void LT_Calibrate_JoyStick();

extern int LT_PC_Speaker_Size;
void LT_Setup(){
	byte character = 0;
	LT_Text_Mode();//Clear screen
	
	//There is a bug somewhere, I had to set this here
	LT_Load_Music = &LT_Null;
	LT_Play_Music = &LT_Null;
	
	//Check_Ram();
	Check_Graphics_Card();
	if (LT_DETECTED_CARD != 1) {
		_printf("\n\n\rERROR VGA:  YOU NEED VGA CARD / NECESITAS UNA TARJETA VGA\n\r$");
		LT_sleep(6);
		LT_Exit();
	}
	LT_Adlib_Detect();
	LT_setup_data[0x16] = LT_DETECTED_CARD+48;
	LT_setupfile = LT_fopen("config.txt",0x02);
	if (!LT_setupfile) {
		_printf("\n\rconfig.txt is missing\n\rNew default file created\n\r$");
		LT_setupfile = LT_fcreate("config.txt");
		LT_fclose(LT_setupfile);
		LT_setupfile = LT_fopen("config.txt",0x01);
		LT_fwrite(LT_setupfile,0xB0,LT_setup_data);
		LT_fclose(LT_setupfile);
		LT_sleep(2);
		LT_setupfile = LT_fopen("config.txt",0x02);
		LT_Text_Mode();
	}
	Read_Setup();
	
	_printf("\n\rRun setup? - Configurar? (Y/N)\n\r$");
	asm in al, 61h; asm and al, 252; asm out 61h, al //Disable speaker

	character = LT_Wait_kbhit();
	Clearkb();
	if ((character == 0x59) || (character == 0x79)){//Y y
		Clearkb();
		_printf("\n\r\nSet hardware scroll - Configura el scroll hardware:$");
		_printf("\n\r\t> 0 = original VGA$");
		_printf("\n\r\t> 1 = clon VGA (more compatible - mas compatible)$");
		_printf("\n\r\t> 2 = other - otro$");
		character = LT_Wait_kbhit()- 48;
		if (character  > 2) {
			if (character == 7) LT_DEBUG = 1;
			else character = 0;
			character = 0;
		}
		if (character == 0) LT_WaitVsync = &LT_WaitVsync_VGA;
		if (character == 1) LT_WaitVsync = &LT_WaitVsync_VGA_Compatible;
		if (character == 2) LT_WaitVsync = &LT_WaitVsync_VGA_crappy;

		if (LT_DEBUG) LT_WaitVsync = &LT_WaitVsync_VGA_Debug;

		LT_SCROLL_MODE = character;
	
		_printf("\n\n\rSelect music mode - Selecciona modo de m£sica:$");
		_printf("\n\r\t> 0 = NONE\n\r\t> 1 = ADLIB/SOUND BLASTER FM$");
		_printf("\n\r\t> SOUND BLASTER MOD/TRACKER (2 = 8000Hz; 3 = 11000Hz; 4 = 22000Hz)\n\r\t$");

		character = LT_Wait_kbhit()- 48;
		if (character == 1) LT_MUSIC_MODE = 1;
		else if (character > 1 && character < 5){
			LT_MUSIC_MODE = 2;
			if (character == 2) LT_MODMASTER_FREQ = 0;
			else if (character == 3) LT_MODMASTER_FREQ = 1;
			else if (character == 4) LT_MODMASTER_FREQ = 2;
			else LT_MODMASTER_FREQ = 1;
		}
		else LT_MUSIC_MODE = 0;
		Clearkb();
		_printf("\n\rSelect sound effect mode - Selecciona modo de efectos de sonido:\n\r\t> 0 = PC Speaker\n\r\t> 1 = ADLIB$");
		character = LT_Wait_kbhit()- 48;
		if ( character > 1 ) character = 1;
		LT_SFX_MODE = character;
		//JOYSTICK
		LT_JOYSTICK = 0;
		_printf("\n\rControl:\n\r\t> 0 = keyboard / teclado\n\r\t> 1 = Joystick$");
		character = LT_Wait_kbhit()- 48;
		Clearkb();
		if (character == 1) {LT_Calibrate_JoyStick();}

		//SAVE SETTINGS
		Save_Setup();
	}
	LT_fclose(LT_setupfile);

    LT_init();
}

void LT_ClearScreen();

void LT_Init(){
	word RAM = 0;
	byte *check;
	word Min_Ram = 0;
	word data_seg; word data_off;
	word i;
	byte sprite_number;

	VGA_ClearPalette();

	LT_sprite_data_offset = 0;

	//SET MODE
	asm mov ax,0x000D;asm int 0x10; //EGA/VGA 320x200x16
	//set 320x240x60
	outport(0x03D4, 0x2C11); // V_RETRACE_END: write protect OFF;
	outport(0x03D4, 0x0D06); // V_TOTAL = 0x06;
	outport(0x03D4, 0x3E07); // OVERFLOW = 0x3E;
	outport(0x03D4, 0xEA10); // V_RETRACE_START = 0xEA
	//outport(0x03D4, 0xAC11); // V_RETRACE_END = 0xAC
	outport(0x03D4, 0xDF12); // V_DISPLAY_END = 0xDF
	outport(0x03D4, 0xE715); // V_BLANK_START = 0xE7
	outport(0x03D4, 0x0616); // V_BLANK_END = 0x06
	outport(0x03D4, 0x8E11); // V_RETRACE_END: back to normal
	outport(0x03D4, 0x1913); // OFFSET: vram width = 50 bytes
	outport(0x03CE, 0x0205); // write mode 2
	outport(0x03CE, 0xFF08); // Set mask register
	outport(0x03C4, 0xFF02); // set map mask to all 4 planes
	outport(0x03CE, 0x0506); // index 6, data 05h

	//Set color indexes
	for (i = 0; i < 16; i++){
		inportb(0x03DA);
		outportb(0x03C0,i);
		outportb(0x03C0,i);
	}
	inportb(0x03DA);
	outportb(0x03C0,0x20);

	LT_memset(VGA,0x00,(320*200)/8);

	LT_vsync();
	CalibrateMaxHblankLength();

	LT_Scroll_Map = &LT_Scroll_Map_GFX;
	LT_Draw_Sprites = &LT_Draw_Sprites_1;

	LT_VRAM_Logical_Width = 50;
	TILE_VRAM = 0xE4E8;//1172x50 fixed in engine
	
	LT_Fade_out();
	LT_ClearScreen();
	
	if (LT_MUSIC_MODE == 0){
		LT_Load_Music = &LT_Null;
		LT_Play_Music = &LT_Null;
		LT_Start_Music = &LT_Null;
		LT_Stop_Music = &LT_Null;
	}
	if (LT_MUSIC_MODE == 1){
		LT_Load_Music = &Load_Music_Adlib;
		LT_Play_Music = &Play_Music_VGM;
		LT_Start_Music = &LT_Start_VGM;
		LT_Stop_Music = &LT_Stop_VGM;
	}
	if (LT_MUSIC_MODE == 2){//MODMASTER
		LT_Load_Music = &LT_MOD_Load;
		LT_Play_Music = &LT_mod_update;
		LT_Start_Music = &LT_Start_MOD;
		LT_Stop_Music = &LT_Stop_MOD;
	}

	LT_old_time_handler = LT_getvect(0x1C);
	LT_destroy_key_handler();
	
	//Allocate mem; only 230KB!!!!
	Min_Ram = 0x39E1;
	
	if (LT_MUSIC_MODE > 0) {
		LT_data = LT_calloc0(Min_Ram + 0x1000 + 1);
		if (!LT_data) {
			LT_MUSIC_MODE = 0;
			_printf("LOW RAM: MUSIC OFF");
			LT_sleep(2);
		}
	}
	if (LT_MUSIC_MODE == 0) LT_data = LT_calloc0(Min_Ram+1);
	if (!LT_data) LT_Error("RAM ERROR",0);
	data_seg = FP_SEG(LT_data);data_off = FP_OFF(LT_data);
	RAM++;//WARINING: keep the MCB (16 bytes), MSDOS will free it when program returns
	LT_sprite_data  = (byte far *) MK_FP(data_seg + RAM,data_off);RAM+=0x1000;//64KB
	//LT_sprite_data2  = (byte far *) MK_FP(data_seg + RAM,data_off);RAM+=0x1000;//64KB
	sprite_id_table  = (byte far *) MK_FP(data_seg + RAM,data_off);RAM+=0x0480;
	sprite = (SPRITE far *) MK_FP(data_seg + RAM,data_off);RAM+=0x4E1;//22 *350 sprite structs
	LT_map_collision = (byte far *) MK_FP(data_seg + RAM,data_off);RAM+=0x0480;
	LT_map_data = (word far *) MK_FP(data_seg + RAM,data_off);RAM+=0x0C00;//512x36 + 512x12; (x2)
	//music
	if (LT_MUSIC_MODE > 0)	{LT_music_sdata = (byte far *) MK_FP(data_seg + RAM,data_off);RAM+=0x1000;}//64KB
	
	//STORE BKG AT FIXED VRAM ADDRESS (EGA/VGA)
	for (sprite_number = 0; sprite_number != 8; sprite_number++){
		sprite[sprite_number].bkg_data = sprite_bkg[sprite_number];
		for (i=0;i<64;i++){
			sprite[sprite_number].frames[i] = LT_Sprite_Empty_Frame;
		}
	}

	LT_memset(LT_sprite_data,0,0x65535);
	//LT_memset(LT_sprite_data1,0,0x65535);
	if (LT_MUSIC_MODE == 2) LT_MODMaster_Init(LT_MODMASTER_FREQ);
}


byte Game_Boy_Adlib[11] = {0xC4,0x1A,0x06,0x8E,0xA6,0xE8,0xF0,0xA0,0x00,0x00,0x02};
byte GameBoy_Sound[24] = {72,72,72,72,72,84,84,84,84,84,84,84,84,84,84,84,84,84,84,84,84,84,84,84};
byte loading_logo = 0;
void LT_Logo(char *file, char *dat_string){
	loading_logo = 1;
	LT_PC_Speaker_Size = 24;
	//LT_Fade_out();
	LT_Load_Image(file,dat_string);
	LT_sleep(2);
	LT_WaitVsync();
	LT_Fade_in();
	if (!LT_SFX_MODE) LT_Play_PC_Speaker_SFX(GameBoy_Sound);
	if (LT_SFX_MODE) {
		int i =8;
		LT_Play_AdLib_SFX(Game_Boy_Adlib,0,3,7);//first note
		while (i){LT_vsync();i--;}
		LT_Play_AdLib_SFX(Game_Boy_Adlib,0,4,7);//second note
	}
	LT_sleep(4);
	LT_Fade_out();
	LT_PC_Speaker_Size = 16;
	Clearkb();
}

void LT_ExitDOS(){
	LT_reset_key_handler();
	LT_Stop_Music();
	LT_Disable_Speaker();
	LT_Fade_out();
	LT_Text_Mode();
	//system("mem");
	LT_free(LT_data);
	LT_Exit();
}

//Pre calculated SIN and COS, Divide to use for smaller circles.
char far LT_SIN[365] = {
0, 0, 1, 1, 3, 5, 6, 8, 10, 12, 13,
15, 17, 19, 20, 22, 24, 25, 27, 29, 30,
32, 34, 35, 37, 39, 40, 42, 43, 45, 46,
48, 50, 51, 52, 54, 55, 57, 58, 60, 61,
62, 64, 65, 66, 68, 69, 70, 71, 73, 74,
75, 76, 77, 78, 79, 80, 81, 82, 83, 84,
85, 86, 87, 88, 89, 89, 90, 91, 92, 92,
93, 93, 94, 95, 95, 96, 96, 97, 97, 97,
98, 98, 98, 99, 99, 99, 99, 99, 99, 99,
99, 100, 99, 99, 99, 99, 99, 99, 99, 99,
98, 98, 98, 97, 97, 97, 96, 96, 95, 95,
94, 93, 93, 92, 92, 91, 90, 89, 89, 88,
87, 86, 85, 84, 83, 82, 81, 80, 79, 78,
77, 76, 75, 74, 73, 71, 70, 69, 68, 66,
65, 64, 62, 61, 60, 58, 57, 55, 54, 52,
51, 50, 48, 46, 45, 43, 42, 40, 39, 37,
35, 34, 32, 30, 29, 27, 25, 24, 22, 20,
19, 17, 15, 13, 12, 10, 8, 6, 5, 3,
1, 0, -2, -4, -6, -7, -9, -11, -13, -14,
-16, -18, -20, -21, -23, -25, -26, -28, -30, -31,
-33, -35, -36, -38, -40, -41, -43, -44, -46, -47,
-49, -50, -52, -53, -55, -56, -58, -59, -61, -62,
-63, -65, -66, -67, -69, -70, -71, -72, -74, -75,
-76, -77, -78, -79, -80, -81, -82, -83, -84, -85,
-86, -87, -88, -89, -90, -90, -91, -92, -93, -93,
-94, -94, -95, -96, -96, -97, -97, -98, -98, -98,
-99, -99, -99, -100, -100, -100, -100, -100, -100, -100,
-100, -100, -100, -100, -100, -100, -100, -100, -100, -100,
-99, -99, -99, -98, -98, -98, -97, -97, -96, -96,
-95, -94, -94, -93, -93, -92, -91, -90, -90, -89,
-88, -87, -86, -85, -84, -83, -82, -81, -80, -79,
-78, -77, -76, -75, -74, -72, -71, -70, -69, -67,
-66, -65, -63, -62, -61, -59, -58, -56, -55, -53,
-52, -51, -49, -47, -46, -44, -43, -41, -40, -38,
-36, -35, -33, -31, -30, -28, -26, -25, -23, -21,
-20, -18, -16, -14, -13, -11, -9, -7, -6, -4, -2,
-1, 0, 0
};

char far LT_COS[365] = {
99, 100, 99, 99, 99, 99, 99, 99, 99, 99,
98, 98, 98, 97, 97, 97, 96, 96, 95, 95,
94, 93, 93, 92, 92, 91, 90, 89, 89, 88,
87, 86, 85, 84, 83, 82, 81, 80, 79, 78,
77, 76, 75, 74, 73, 71, 70, 69, 68, 66,
65, 64, 62, 61, 60, 58, 57, 55, 54, 52,
51, 50, 48, 46, 45, 43, 42, 40, 39, 37,
35, 34, 32, 30, 29, 27, 25, 24, 22, 20,
19, 17, 15, 13, 12, 10, 8, 6, 5, 3,
1, 0, -2, -4, -6, -7, -9, -11, -13, -14,
-16, -18, -20, -21, -23, -25, -26, -28, -30, -31,
-33, -35, -36, -38, -40, -41, -43, -44, -46, -47,
-49, -50, -52, -53, -55, -56, -58, -59, -61, -62,
-63, -65, -66, -67, -69, -70, -71, -72, -74, -75,
-76, -77, -78, -79, -80, -81, -82, -83, -84, -85,
-86, -87, -88, -89, -90, -90, -91, -92, -93, -93,
-94, -94, -95, -96, -96, -97, -97, -98, -98, -98,
-99, -99, -99, -100, -100, -100, -100, -100, -100, -100,
-100, -100, -100, -100, -100, -100, -100, -100, -100, -100,
-99, -99, -99, -98, -98, -98, -97, -97, -96, -96,
-95, -94, -94, -93, -93, -92, -91, -90, -90, -89,
-88, -87, -86, -85, -84, -83, -82, -81, -80, -79,
-78, -77, -76, -75, -74, -72, -71, -70, -69, -67,
-66, -65, -63, -62, -61, -59, -58, -56, -55, -53,
-52, -51, -49, -47, -46, -44, -43, -41, -40, -38,
-36, -35, -33, -31, -30, -28, -26, -25, -23, -21,
-20, -18, -16, -14, -13, -11, -9, -7, -6, -4, -2,
-1, 0, 0 ,0,
 0, 1, 1, 3, 5, 6, 8, 10, 12, 13,
15, 17, 19, 20, 22, 24, 25, 27, 29, 30,
32, 34, 35, 37, 39, 40, 42, 43, 45, 46,
48, 50, 51, 52, 54, 55, 57, 58, 60, 61,
62, 64, 65, 66, 68, 69, 70, 71, 73, 74,
75, 76, 77, 78, 79, 80, 81, 82, 83, 84,
85, 86, 87, 88, 89, 89, 90, 91, 92, 92,
93, 93, 94, 95, 95, 96, 96, 97, 97, 97,
98, 98, 98, 99, 99, 99, 99, 99, 99, 99,
};
