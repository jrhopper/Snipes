#include <Windows.h>
#include <MMSystem.h>
#include <stdio.h>
#include <math.h>
#pragma comment(lib,"winmm.lib")

typedef unsigned char Uchar;
typedef unsigned int Uint;
typedef unsigned long long QWORD;

#define inrange(n,a,b) ((Uint)((n)-(a))<=(Uint)((b)-(a)))

template <size_t size>
char (*__strlength_helper(char const (&_String)[size]))[size];
#define strlength(_String) (sizeof(*__strlength_helper(_String))-1)

#define STRING_WITH_LEN(s) s, strlength(s)

QWORD perf_freq;

WORD GetTickCountWord()
{
	QWORD time;
	QueryPerformanceCounter((LARGE_INTEGER*)&time);
	return (WORD)(time * 13125000 / perf_freq);
}

HANDLE input;
HANDLE output;

#define TONE_SAMPLE_RATE 48000
#define WAVE_BUFFER_LENGTH 200
#define WAVE_BUFFER_COUNT 11
HWAVEOUT waveOutput;
WAVEHDR waveHeader[WAVE_BUFFER_COUNT];
static double toneFreq = 0;
Uint tonePhase;
SHORT toneBuf[WAVE_BUFFER_LENGTH * WAVE_BUFFER_COUNT];
void CALLBACK WaveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	if (uMsg != WOM_DONE)
		return;
	if (toneFreq == 0.)
		return;
	WAVEHDR *currentWaveHeader = (WAVEHDR*)dwParam1;
	for (Uint i=0; i<WAVE_BUFFER_LENGTH; i++)
	{
		((SHORT*)currentWaveHeader->lpData)[i] = fmod(tonePhase * toneFreq, 1.) < 0.5 ? 0 : 0x2000;
		tonePhase++;
	}
	waveOutWrite(hwo, currentWaveHeader, sizeof(SHORT)*WAVE_BUFFER_LENGTH);
}
void PlayTone(Uint freqnum)
{
	bool soundAlreadyPlaying = toneFreq != 0.;
	toneFreq = (13125000. / (TONE_SAMPLE_RATE * 11)) / freqnum;
	tonePhase = 0;
	if (soundAlreadyPlaying)
		return;
	for (Uint i=0; i<WAVE_BUFFER_COUNT; i++)
		WaveOutProc(waveOutput, WOM_DONE, 0, (DWORD_PTR)&waveHeader[i], 0);
}
void ClearSound()
{
	toneFreq = 0.;
}

#define KEYSTATE_MOVE_RIGHT (1<<0)
#define KEYSTATE_MOVE_LEFT  (1<<1)
#define KEYSTATE_MOVE_DOWN  (1<<2)
#define KEYSTATE_MOVE_UP    (1<<3)
#define KEYSTATE_FIRE_RIGHT (1<<4)
#define KEYSTATE_FIRE_LEFT  (1<<5)
#define KEYSTATE_FIRE_DOWN  (1<<6)
#define KEYSTATE_FIRE_UP    (1<<7)

static bool forfeit_match = false;
static bool sound_enabled = true;
static bool shooting_sound_enabled = false;
static bool spacebar_state = false;
static Uchar keyboard_state = 0;
Uint PollKeyboard()
{
	for (;;)
	{
		DWORD numread = 0;
		INPUT_RECORD record;
		if (!PeekConsoleInput(input, &record, 1, &numread))
			break;
		if (!numread)
			break;
		ReadConsoleInput(input, &record, 1, &numread);
		if (!numread)
			break;
		if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown)
		{
			if (record.Event.KeyEvent.wVirtualKeyCode == VK_F1)
				sound_enabled ^= true;
			if (record.Event.KeyEvent.wVirtualKeyCode == VK_F2)
				shooting_sound_enabled ^= true;
		}
	}
	Uint state = 0;
	forfeit_match = GetKeyState(VK_ESCAPE) < 0;
	if (GetKeyState(VK_RIGHT) < 0) state |= KEYSTATE_MOVE_RIGHT;
	if (GetKeyState(VK_LEFT ) < 0) state |= KEYSTATE_MOVE_LEFT;
	if (GetKeyState(VK_DOWN ) < 0) state |= KEYSTATE_MOVE_DOWN;
	if (GetKeyState(VK_CLEAR) < 0) state |= KEYSTATE_MOVE_DOWN;
	if (GetKeyState(VK_UP   ) < 0) state |= KEYSTATE_MOVE_UP;
	if (GetKeyState('D'     ) < 0) state |= KEYSTATE_FIRE_RIGHT;
	if (GetKeyState('A'     ) < 0) state |= KEYSTATE_FIRE_LEFT;
	if (GetKeyState('S'     ) < 0) state |= KEYSTATE_FIRE_DOWN;
	if (GetKeyState('X'     ) < 0) state |= KEYSTATE_FIRE_DOWN;
	if (GetKeyState('W'     ) < 0) state |= KEYSTATE_FIRE_UP;
	spacebar_state = GetKeyState(VK_SPACE) < 0;
	if ((state & (KEYSTATE_MOVE_RIGHT | KEYSTATE_MOVE_LEFT)) == (KEYSTATE_MOVE_RIGHT | KEYSTATE_MOVE_LEFT) ||
		(state & (KEYSTATE_MOVE_DOWN  | KEYSTATE_MOVE_UP  )) == (KEYSTATE_MOVE_DOWN  | KEYSTATE_MOVE_UP  ))
		state &= ~(KEYSTATE_MOVE_RIGHT | KEYSTATE_MOVE_LEFT | KEYSTATE_MOVE_DOWN | KEYSTATE_MOVE_UP);
	if ((state & (KEYSTATE_FIRE_RIGHT | KEYSTATE_FIRE_LEFT)) == (KEYSTATE_FIRE_RIGHT | KEYSTATE_FIRE_LEFT) ||
		(state & (KEYSTATE_FIRE_DOWN  | KEYSTATE_FIRE_UP  )) == (KEYSTATE_FIRE_DOWN  | KEYSTATE_FIRE_UP  ))
		state &= ~(KEYSTATE_FIRE_RIGHT | KEYSTATE_FIRE_LEFT | KEYSTATE_FIRE_DOWN | KEYSTATE_FIRE_UP);
	return state;
}

#define WINDOW_WIDTH  40
#define WINDOW_HEIGHT 25

void WriteTextMem(Uint count, Uint dst, WORD *src)
{
	COORD pos;
	pos.X = dst / 2 % WINDOW_WIDTH;
	pos.Y = dst / 2 / WINDOW_WIDTH;
	SetConsoleCursorPosition(output, pos);
	while (count--)
	{
		SetConsoleTextAttribute(output, ((BYTE*)src)[1]);
		DWORD operationSize;
		WriteConsole(output, (BYTE*)src, 1, &operationSize, 0);
		src++;
	}
}

DWORD ReadConsole_wrapper(char buffer[], DWORD bufsize)
{
	DWORD numread, numreadWithoutNewline;
	ReadConsole(input, buffer, bufsize, &numread, NULL);
	if (!numread)
		return numread;

	numreadWithoutNewline = numread;
	if (buffer[numread-1] == '\n')
	{
		numreadWithoutNewline--;
		if (numread > 1 && buffer[numread-2] == '\r')
			numreadWithoutNewline--;
		return numreadWithoutNewline;
	}
	else
	if (buffer[numread-1] == '\r')
		numreadWithoutNewline--;

	if (buffer[numread-1] != '\n')
	{
		char dummy;
		do
			ReadConsole(input, &dummy, 1, &numread, NULL);
		while (numread && dummy != '\n');
	}
	return numreadWithoutNewline;
}

WORD random_seed_lo, random_seed_hi;
WORD GetRandom(WORD mask)
{
	random_seed_lo *= 2;
	if (random_seed_lo  > 941)
		random_seed_lo -= 941;
	random_seed_hi *= 2;
	if (random_seed_hi  > 947)
		random_seed_hi -= 947;
	return (random_seed_lo + random_seed_hi) & mask;
}

Uint skillLevelLetter = 0;
Uint skillLevelNumber = 1;

void ParseSkillLevel(char *skillLevel, DWORD skillLevelLength)
{
	Uint skillLevelNumberTmp = 0;
	for (; skillLevelLength; skillLevel++, skillLevelLength--)
	{
		if (skillLevelNumberTmp >= 0x80)
		{
			// strange behavior, but this is what the original game does
			skillLevelNumber = 1;
			return;
		}
		char ch = *skillLevel;
		if (inrange(ch, 'a', 'z'))
			skillLevelLetter = ch - 'a';
		else
		if (inrange(ch, 'A', 'Z'))
			skillLevelLetter = ch - 'A';
		else
		if (inrange(ch, '0', '9'))
			skillLevelNumberTmp = skillLevelNumberTmp * 10 + (ch - '0');
	}
	if (inrange(skillLevelNumberTmp, 1, 9))
		skillLevelNumber = skillLevelNumberTmp;
}

void ReadSkillLevel()
{
	char skillLevel[0x80] = {0};
	DWORD skillLevelLength = ReadConsole_wrapper(skillLevel, _countof(skillLevel));

	if (skillLevel[skillLevelLength-1] == '\n')
		skillLevelLength--;
	if (skillLevel[skillLevelLength-1] == '\r')
		skillLevelLength--;

	for (Uint i=0; i<skillLevelLength; i++)
		if (skillLevel[i] != ' ')
		{
			ParseSkillLevel(skillLevel + i, skillLevelLength - i);
			break;
		}
}

static Uchar skillThing1Table  ['Z'-'A'+1] = {2, 3, 4, 3, 4, 4, 3, 4, 3, 4, 4, 5, 3, 4, 3, 4, 3, 4, 3, 4, 4, 5, 4, 4, 5, 5};
static bool  skillThing2Table  ['Z'-'A'+1] = {0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};
static bool  rubberBulletTable ['Z'-'A'+1] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static Uchar skillThing3Table  ['Z'-'A'+1] = {0x7F, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x1F, 0x7F, 0x7F, 0x3F, 0x3F, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x1F, 0x3F, 0x1F, 0x1F, 0x0F};
static Uchar maxSnipesTable    ['9'-'1'+1] = { 10,  20,  30,  40,  60,  80, 100, 120, 150};
static Uchar numGeneratorsTable['9'-'1'+1] = {  3,   3,   4,   4,   5,   5,   6,   8,  10};
static Uchar numLivesTable     ['9'-'1'+1] = {  5,   5,   5,   5,   5,   4,   4,   3,   2};

bool enableElectricWalls, skillThing2, skillThing7, enableRubberBullets;
Uchar skillThing1, skillThing3, maxSnipes, numGenerators, numLives;

Uchar data_1D0, data_2AA;
static Uchar data_2B4, data_2B3, data_2B2, data_2C0, data_2AF, data_2B0;
static WORD data_290, data_28E, data_292, data_1EA, data_1E2;
static SHORT data_1DE, data_1E0, data_1E4, data_1E6, data_1E8;

static BYTE data_350[320];

#define MAZE_CELL_WIDTH  8
#define MAZE_CELL_HEIGHT 6
#define MAZE_WIDTH  (MAZE_CELL_WIDTH  * 16)
#define MAZE_HEIGHT (MAZE_CELL_HEIGHT * 20)

static WORD maze[MAZE_WIDTH * MAZE_HEIGHT];

void outputText(BYTE color, WORD count, WORD dst, char *src)
{
	COORD pos;
	pos.X = dst / 2 % WINDOW_WIDTH;
	pos.Y = dst / 2 / WINDOW_WIDTH;
	SetConsoleCursorPosition(output, pos);
	SetConsoleTextAttribute(output, color);
	DWORD operationSize;
	WriteConsole(output, src, count, &operationSize, 0);
}

void outputNumber(BYTE color, WORD count, WORD dst, Uint number)
{
	char textbuf[strlength("4294967295")+1];
	sprintf_s(textbuf, sizeof(textbuf), "%*u", count, number);
	outputText(color, count, dst, textbuf);
}

static char statusLine[] = "\xDA\xBF\xB3\x01\x1A\xB3\x02\xB3""Skill""\xC0\xD9\xB3\x01\x1A\xB3\x02\xB3""Time  Men Left                 Score     0  0000001 Man Left\x65";

void outputHUD()
{
	char skillLetter = skillLevelLetter + 'A';
	memset((char*)maze, ' ', 40);
	outputText  (0x17, 40, 2* 0, (char*)maze);
	outputText  (0x17, 40, 2*40, (char*)maze);
	outputText  (0x17,  2, 2* 0, statusLine);
	outputNumber(0x13,  2, 2* 3, 0);
	outputText  (0x17,  1, 2* 6, statusLine+2);
	outputText  (0x13,  2, 2* 8, statusLine+3);
	outputNumber(0x13,  4, 2*11, 0);
	outputText  (0x17,  1, 2*16, statusLine+5);
	outputText  (0x13,  1, 2*18, statusLine+6);
	outputNumber(0x13,  4, 2*20, 0);
	outputText  (0x17,  1, 2*25, statusLine+7);
	outputText  (0x17,  5, 2*27, statusLine+8);
	outputNumber(0x17,  1, 2*38, skillLevelNumber);
	outputText  (0x17,  1, 2*37, &skillLetter);
	outputText  (0x17,  2, 2*40, statusLine+13);
	outputText  (0x17,  1, 2*46, statusLine+15);
	outputText  (0x17,  2, 2*48, statusLine+16);
	outputText  (0x17,  1, 2*56, statusLine+18);
	outputText  (0x17,  1, 2*58, statusLine+19);
	outputText  (0x17,  1, 2*65, statusLine+20);
	outputText  (0x17,  4, 2*67, statusLine+21);
	memcpy(maze, statusLine+25, 40);
	((char*)maze)[0] = numLives + '0';
	((char*)maze)[1] = 0;
	if (numLives == 1)
		((char*)maze)[6] = 'a';
	outputText  (0x17, 40, 2*80, (char*)maze);

	data_2B4 = 0xFF;
	data_2B3 = 0xFF;
	data_2B2 = 0xFF;
	data_290 = 0xFFFF;
	data_28E = 0xFFFF;
	data_2C0 = 0xFF;
	data_292 = 0xFFFF;
}

void CreateMaze_helper()
{
	switch (data_2AF)
	{
	case 0:
		data_1E0 -= 0x10;
		if (data_1E0 < 0)
			data_1E0 += sizeof(data_350);
		break;
	case 1:
		data_1E0 += 0x10;
		if (data_1E0 >= sizeof(data_350))
			data_1E0 -= sizeof(data_350);
		break;
	case 2:
		if ((data_1E0 & 0xF) == 0)
			data_1E0 += 0xF;
		else
			data_1E0--;
		break;
	case 3:
		if ((data_1E0 & 0xF) == 0xF)
			data_1E0 -= 0xF;
		else
			data_1E0++;
		break;
	}
}

void CreateMaze()
{
	memset(data_350, 0xF, sizeof(data_350));
	data_350[0] = 0xE;
	data_350[1] = 0xD;
	data_1EA = 0x13E;

main_283:
	if (!data_1EA)
		goto main_372;
	do
		data_1DE = GetRandom(0x1FF);
	while (data_1DE >= sizeof(data_350));
	if (data_350[data_1DE] != 0xF)
		goto main_283;
	data_1E2 = GetRandom(3);
	data_1E4 = 0;
	for (;;)
	{
		if (data_1E4 > 3)
			goto main_283;
		data_2AF = data_1E2 & 3;
		data_1E0 = data_1DE;
		CreateMaze_helper();
		if (data_350[data_1E0] != 0xF)
			break;
		data_1E2++;
		data_1E4++;
	}
	data_1EA--;
	static BYTE data_EA3[] = {8, 4, 2, 1};
	static BYTE data_EA7[] = {4, 8, 1, 2};
	data_350[data_1E0] ^= data_EA7[data_2AF];
	data_350[data_1DE] ^= data_EA3[data_2AF];
	data_1E2 = data_1DE;
main_30E:
	data_2AF = (BYTE)GetRandom(3);
	data_2B0 = (BYTE)GetRandom(3) + 1;
	data_1E0 = data_1E2;
	for (;;)
	{
		CreateMaze_helper();
		if (!data_2B0 || data_350[data_1E0] != 0xF)
			break;
		data_350[data_1E0] ^= data_EA7[data_2AF];
		data_350[data_1E2] ^= data_EA3[data_2AF];
		data_1EA--;
		data_2B0--;
		data_1E2 = data_1E0;
	}
	if (data_2B0)
		goto main_283;
	goto main_30E;
main_372:
	for (data_1DE = 1; data_1DE <= 0x40; data_1DE++)
	{
		do
			data_1E0 = GetRandom(0x1FF);
		while (data_1E0 >= sizeof(data_350));
		data_2AF = (BYTE)GetRandom(3);
		data_350[data_1E0] &= ~data_EA3[data_2AF];
		CreateMaze_helper();
		data_350[data_1E0] &= ~data_EA7[data_2AF];
	}
	for (Uint i=0; i<_countof(maze); i++)
		maze[i] = 0x920;
	data_1E4 = 0;
	data_1E2 = 0;
	for (data_1DE = 0; data_1DE <= 0x13; data_1DE++)
	{
		for (data_1E0 = 0; data_1E0 <= 0xF; data_1E0++)
		{
			if (data_350[data_1E2] & 8)
				for (Uint i=0; i<MAZE_CELL_WIDTH-1; i++)
					maze[data_1E4 + 1 + i] = 0x9CD;
			if (data_350[data_1E2] & 2)
			{
				data_1E6 = data_1E4 + MAZE_WIDTH;
				for (data_1E8 = 1; data_1E8 <= 5; data_1E8++)
				{
					maze[data_1E6] = 0x9BA;
					data_1E6 += MAZE_WIDTH;
				}
			}
			if (data_1DE)
				data_1E6 = data_1E2 - (!data_1E0 ? 1 : 0x11);
			else
			{
				if (!data_1E0)
					data_1E6 = sizeof(data_350) - 1;
				else
					data_1E6 = data_1E2 + sizeof(data_350) - 0x11;
			}
			static BYTE data_EAB[] = {0x20, 0xBA, 0xBA, 0xBA, 0xCD, 0xBC, 0xBB, 0xB9, 0xCD, 0xC8, 0xC9, 0xCC, 0xCD, 0xCA, 0xCB, 0xCE};
			maze[data_1E4] = data_EAB[data_350[data_1E2]&0xA | data_350[data_1E6]&0x5] + 0x900;
			data_1E4 += 8;
			data_1E2++;
		}
		data_1E4 += MAZE_WIDTH * 5;
	}
	FILE *f = fopen("mazey.bin", "wb");
	for (Uint i=0; i<sizeof(maze)/2; i++)
		fwrite(&maze[i], 1, 1, f);
	fclose(f);
}

void main_FBD()
{
}
void main_25E2(Uchar a, Uchar b)
{
}
bool main_81A()
{
	return false;
}
void main_125C()
{
}
void main_2124()
{
}
void main_1EC1()
{
}
void main_10C9()
{
}
bool main_1AB0()
{
	keyboard_state = PollKeyboard();
	return false;
}
void main_1D64()
{
}
void main_260E()
{
}
void main_9BB()
{
}

int main(int argc, char* argv[])
{
	input  = GetStdHandle(STD_INPUT_HANDLE);
	output = GetStdHandle(STD_OUTPUT_HANDLE);

	SetConsoleCP      (437);
	SetConsoleOutputCP(437);
	
	CONSOLE_CURSOR_INFO cursorInfo;
	GetConsoleCursorInfo(output, &cursorInfo);

	COORD windowSize;
	windowSize.X = WINDOW_WIDTH;
	windowSize.Y = WINDOW_HEIGHT;
	SMALL_RECT window;
	window.Left   = 0;
	window.Top    = 0;
	window.Right  = windowSize.X-1;
	window.Bottom = windowSize.Y-1;
	SetConsoleWindowInfo(output, TRUE, &window);
	SetConsoleScreenBufferSize(output, windowSize);

	//timeBeginPeriod(1);

	QueryPerformanceFrequency((LARGE_INTEGER*)&perf_freq);
	perf_freq *= 11 * 65535;

	{
		WAVEFORMATEX waveFormat;
		waveFormat.wFormatTag = WAVE_FORMAT_PCM;
		waveFormat.nChannels = 1;
		waveFormat.nSamplesPerSec = TONE_SAMPLE_RATE;
		waveFormat.nAvgBytesPerSec = TONE_SAMPLE_RATE * 2;
		waveFormat.nBlockAlign = 2;
		waveFormat.wBitsPerSample = 16;
		waveFormat.cbSize = 0;
		MMRESULT result = waveOutOpen(&waveOutput, WAVE_MAPPER, &waveFormat, (DWORD_PTR)WaveOutProc, NULL, CALLBACK_FUNCTION);
		if (result != MMSYSERR_NOERROR)
		{
			fprintf(stderr, "Error opening wave output\n");
			//timeEndPeriod(1);
			return -1;
		}
		for (Uint i=0; i<WAVE_BUFFER_COUNT; i++)
		{
			waveHeader[i].lpData = (LPSTR)&toneBuf[WAVE_BUFFER_LENGTH * i];
			waveHeader[i].dwBufferLength = sizeof(SHORT)*WAVE_BUFFER_LENGTH;
			waveHeader[i].dwBytesRecorded = 0;
			waveHeader[i].dwUser = i;
			waveHeader[i].dwFlags = 0;
			waveHeader[i].dwLoops = 0;
			waveHeader[i].lpNext = 0;
			waveHeader[i].reserved = 0;
			waveOutPrepareHeader(waveOutput, &waveHeader[i], sizeof(waveHeader[i]));
		}
	}

	DWORD operationSize; // dummy in most circumstances
	COORD pos;

	pos.X = 0;
	pos.Y = 0;
	FillConsoleOutputAttribute(output, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED, windowSize.X*windowSize.Y, pos, &operationSize);
	SetConsoleTextAttribute(output, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
	WriteConsole(output, STRING_WITH_LEN("Enter skill level (A-Z)(1-9): "), &operationSize, 0);
	ReadSkillLevel();

	//WriteTextMem(5, 80, (WORD*)"H\x48""e\x39""l\x2A""l\x1B""o\x0C");

	WORD tick_count = GetTickCountWord();
	random_seed_lo = (BYTE)tick_count;
	if (!random_seed_lo)
		random_seed_lo = 444;
	random_seed_hi = tick_count >> 8;
	if (!random_seed_hi)
		random_seed_hi = 555;

	for (;;)
	{
		//printf("Skill: %c%c\n", skillLevelLetter + 'A', skillLevelNumber + '0');

		enableElectricWalls = skillLevelLetter >= 'M'-'A';
		skillThing1           = skillThing1Table  [skillLevelLetter];
		skillThing2           = skillThing2Table  [skillLevelLetter];
		skillThing3           = skillThing3Table  [skillLevelLetter];
		maxSnipes             = maxSnipesTable    [skillLevelNumber-1];
		numGenerators         = numGeneratorsTable[skillLevelNumber-1];
		numLives              = numLivesTable     [skillLevelNumber-1];
		skillThing7           = skillLevelLetter < 'W'-'A';
		data_2AA              = 2;
		enableRubberBullets   = rubberBulletTable [skillLevelLetter];

		SetConsoleMode(output, 0);
		cursorInfo.bVisible = FALSE;
		SetConsoleCursorInfo(output, &cursorInfo);

		data_1D0 = 0;
		outputHUD();
		CreateMaze();
		main_FBD();
		main_25E2(0, 0xFF);

		/*StartTone(2711);
		for (;;)
			Sleep(1000);*/

		/*COORD moveto;
		moveto.X = 2;
		moveto.Y = 2;
		CHAR_INFO backgroundFill;
		backgroundFill.Char.AsciiChar = ' ';
		backgroundFill.Attributes = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED | BACKGROUND_BLUE;
		ScrollConsoleScreenBuffer(output, &window, NULL, moveto, &backgroundFill);*/

		for (;;)
		{
			if (forfeit_match || main_81A())
				break;

			for (;;)
			{
				Sleep(1);
				WORD tick_count2 = GetTickCountWord();
				if (tick_count2 != tick_count)
				{
					tick_count = tick_count2;
					break;
				}
			}

			main_125C();
			main_2124();
			main_1EC1();
			main_10C9();

			if (main_1AB0())
				break;

			main_1D64();
			main_260E();
			main_9BB();
		}

		ClearSound();
		forfeit_match = false;

		cursorInfo.bVisible = TRUE;
		SetConsoleCursorInfo(output, &cursorInfo);
		COORD pos;
		pos.X = 0;
		pos.Y = 25-1;
		SetConsoleCursorPosition(output, pos);
		SetConsoleTextAttribute(output, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
		SetConsoleMode(output, ENABLE_PROCESSED_OUTPUT);
		for (;;)
		{
			SetConsoleMode(input, ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
			WriteConsole(output, STRING_WITH_LEN("Play another game? (Y or N) "), &operationSize, 0);
			char playAgain;
			ReadConsole_wrapper(&playAgain, 1);
			if (playAgain == 'Y' || playAgain == 'y')
				goto do_play_again;
			if (playAgain == 'N' || playAgain == 'n')
				goto do_not_play_again;
		}
	do_play_again:
		WriteConsole(output, STRING_WITH_LEN("Enter new skill level (A-Z)(1-9): "), &operationSize, 0);
		ReadSkillLevel();
	}
do_not_play_again:

	for (Uint i=0; i<WAVE_BUFFER_COUNT; i++)
		waveOutUnprepareHeader(waveOutput, &waveHeader[i], sizeof(waveHeader[i]));
	waveOutClose(waveOutput);

	//timeEndPeriod(1);

	return 0;
}
