/*
 * Netris -- A free networked version of T*tris
 * Copyright (C) 1994-2016  Mark H. Weaver et al
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define NOEXT
#include "netris.h"
#include <time.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>

int netType;
char keyTable[KT_numKeys + 1];

static char *keyNames[KT_numKeys+1] = {
	"Left", "FullLeft","Rotate", "Right", "FullRight", "Drop", "Down", "ToggleSpy", "Pause",
	"Faster", "Redraw", "New", "Quit", NULL };

static char *gameNames[GT_len] = { "OnePlayer", "ClassicTwo" };

static int dropModeEnable = 0;
static char *robotProg;

static int wonLast = 0;
int lost = 0, won = 0;
enum States gameState = STATE_STARTING;

ExtFunc void MapKeys(char *newKeys)
{
	int i, k, ch;
	char used[256];
	int errs = 0;

	/* XXX assumptions about ASCII encoding here */
	for (i = k = 0; newKeys[i] && k < KT_numKeys; i++,k++) {
		if (newKeys[i] == '^' && newKeys[i+1])
			keyTable[k] = toupper(newKeys[++i]) - ('A' - 1);
		else
			keyTable[k] = newKeys[i];
	}
	memset(used, 0, sizeof(used));
	for (k = 0; k < KT_numKeys; k++) {
		ch = (unsigned char) keyTable[k];
		if (used[ch]) {
			if (iscntrl(ch) && ch < ' ')
				sprintf(scratch, "Ctrl-%c", ch + ('A' - 1));
			else if (isprint(ch))
				sprintf(scratch, "\"%c\"", ch);
			else
				sprintf(scratch, "0x%X", ch);
			if (!errs)
				fprintf(stderr, "Duplicate key mappings:\n");
			errs++;
			fprintf(stderr, "  %s mapped to both %s and %s\n",
					scratch, keyNames[used[ch]-1], keyNames[k]);
		}
		used[ch] = k + 1;
	}
	if (errs)
		exit(1);
}

ExtFunc int StartNewPiece(int scr, Shape *shape)
{
	curShape[scr] = shape;
	curY[scr] = boardVisible[scr] + 4;
	curX[scr] = boardWidth[scr] / 2;
	while (!ShapeVisible(shape, scr, curY[scr], curX[scr]))
		--curY[scr];
	if (!ShapeFits(shape, scr, curY[scr], curX[scr]))
		return 0;
	PlotShape(shape, scr, curY[scr], curX[scr], 1);
	return 1;
}

ExtFunc void OneGame(int scr, int scr2)
{
	MyEvent event;
	int linesCleared, changed = 0;
	int spied = 0, spying = 0, dropMode = 0;
	int oldPaused = 0, paused = 0, pausedByMe = 0, pausedByThem = 0;
	long pauseTimeLeft;
	int pieceCount = 0;
	int key;
	char *p, *cmd;

	myLinesCleared = opponentLinesCleared = 0;
	speed = stepDownInterval;
	ResetBaseTime();
	InitBoard(scr);
	if (scr2 >= 0) {
		spied = 1;
		spying = 1;
		InitBoard(scr2);
		UpdateOpponentDisplay();
	}
	ClearStatus();
	ShowDisplayInfo();
	SetITimer(speed, speed);
	if (robotEnable) {
		RobotCmd(0, "GameType %s\n", gameNames[gameType]);
		RobotCmd(0, "BoardSize 0 %d %d\n",
				boardVisible[scr], boardWidth[scr]);
		if (scr2 >= 0) {
			RobotCmd(0, "BoardSize 1 %d %d\n",
					boardVisible[scr2], boardWidth[scr2]);
			RobotCmd(0, "Opponent 1 %s %s\n", opponentName, opponentHost);
			if (opponentFlags & SCF_usingRobot)
				RobotCmd(0, "OpponentFlag 1 robot\n");
			if (opponentFlags & SCF_fairRobot)
				RobotCmd(0, "OpponentFlag 1 fairRobot\n");
		}
		RobotCmd(0, "TickLength %.3f\n", speed / 1.0e6);
		RobotCmd(0, "BeginGame\n");
		RobotTimeStamp();
	}
	while (StartNewPiece(scr, ChooseOption(stdOptions))) {
		if (robotEnable && !fairRobot)
			RobotCmd(1, "NewPiece %d\n", ++pieceCount);
		if (spied) {
			short shapeNum;
			netint2 data[1];

			shapeNum = ShapeToNetNum(curShape[scr]);
			data[0] = hton2(shapeNum);
			SendPacket(NP_newPiece, sizeof(data), data);
		}
		for (;;) {
			changed = RefreshBoard(scr) || changed;
			if (spying)
				changed = RefreshBoard(scr2) || changed;
			if (changed) {
				RefreshScreen();
				changed = 0;
			}
			CheckNetConn();
			switch (WaitMyEvent(&event, EM_any)) {
				case E_alarm:
					if (!MovePiece(scr, -1, 0))
						goto nextPiece;
					else if (spied)
						SendPacket(NP_down, 0, NULL);
					break;
				case E_key:
					p = strchr(keyTable, event.u.key);
					key = p - keyTable;
					if (robotEnable) {
						RobotCmd(1, "UserKey %d %s\n",
								(int)(unsigned char)event.u.key,
								p ? keyNames[key] : "?");
						break;
					}
					if (!p)
						break;
				keyEvent:
					if (paused) {
						if (key == KT_quit)
							exit(0);
						if (key != KT_pause && key != KT_redraw)
							break;
					}
					switch(key) {
						case KT_left:
							if (MovePiece(scr, 0, -1) && spied)
								SendPacket(NP_left, 0, NULL);
							break;
						case KT_full_left: {
							int i = 0;
							for(;i < MAX_BOARD_WIDTH; i++){
								if (MovePiece(scr, 0, -1) && spied)
									SendPacket(NP_left, 0, NULL);
							}
							break;
						}
						case KT_right:
							if (MovePiece(scr, 0, 1) && spied)
								SendPacket(NP_right, 0, NULL);
							break;
						case KT_full_right: {
							int i = 0;
							for(; i < MAX_BOARD_WIDTH; i++){
								if (MovePiece(scr, 0, 1) && spied)
									SendPacket(NP_right, 0, NULL);
							}
							break;
						}
						case KT_rotate:
							if (RotatePiece(scr) && spied)
								SendPacket(NP_rotate, 0, NULL);
							break;
						case KT_down:
							if (MovePiece(scr, -1, 0) && spied)
								SendPacket(NP_down, 0, NULL);
							break;
						case KT_toggleSpy:
							spying = (!spying) && (scr2 >= 0);
							break;
						case KT_drop:
							if (DropPiece(scr) > 0) {
								if (spied)
									SendPacket(NP_drop, 0, NULL);
								SetITimer(speed, speed);
							}
							dropMode = dropModeEnable;
							break;
						case KT_pause:
							pausedByMe = !pausedByMe;
							if (gameType == GT_classicTwo) {
								netint2 data[1];

								data[0] = hton2(pausedByMe);
								SendPacket(NP_pause, sizeof(data), data);
							}
							paused = pausedByMe || pausedByThem;
							if (robotEnable)
								RobotCmd(1, "Pause %d %d\n", pausedByMe,
									pausedByThem);
							ShowPause(pausedByMe, pausedByThem);
							changed = 1;
							break;
						case KT_faster:
							if (gameType != GT_onePlayer)
								break;
							speed = speed * 0.8;
							SetITimer(speed, SetITimer(0, 0));
							ShowDisplayInfo();
							changed = 1;
							break;
						case KT_redraw:
							ScheduleFullRedraw();
							if (paused)
								RefreshScreen();
							break;
						case KT_quit:
							exit(0);
							break;

					}
					if (dropMode && DropPiece(scr) > 0) {
						if (spied)
							SendPacket(NP_drop, 0, NULL);
						SetITimer(speed, speed);
					}
					break;
				case E_robot:
				{
					int num;

					cmd = event.u.robot.data;
					if ((p = strchr(cmd, ' ')))
						*p++ = 0;
					else
						p = cmd + strlen(cmd);
					for (key = 0; keyNames[key]; ++key)
						if (!strcmp(keyNames[key], cmd) &&
								(fairRobot || (1 == sscanf(p, "%d", &num) &&
									num == pieceCount)))
							goto keyEvent;
					if (!strcmp(cmd, "Message")) {
						Message(p);
						changed = 1;
					}
					break;
				}
				case E_net:
					switch(event.u.net.type) {
						case NP_giveJunk:
						{
							netint2 data[2];
							short column;

							memcpy(data, event.u.net.data, sizeof(data[0]));
							column = Random(0, boardWidth[scr]);
							data[1] = hton2(column);
							InsertJunk(scr, ntoh2(data[0]), column);
							if (spied)
								SendPacket(NP_insertJunk, sizeof(data), data);
							break;
						}
						case NP_newPiece:
						{
							short shapeNum;
							netint2 data[1];

							FreezePiece(scr2);
							memcpy(data, event.u.net.data, sizeof(data));
							shapeNum = ntoh2(data[0]);
							StartNewPiece(scr2, NetNumToShape(shapeNum));
							break;
						}
						case NP_down:
							MovePiece(scr2, -1, 0);
							break;
						case NP_left:
							MovePiece(scr2, 0, -1);
							break;
						case NP_right:
							MovePiece(scr2, 0, 1);
							break;
						case NP_rotate:
							RotatePiece(scr2);
							break;
						case NP_drop:
							DropPiece(scr2);
							break;
						case NP_clear:
							{
								int cleared = ClearFullLines(scr2);
								if (cleared) {
									opponentLinesCleared += cleared;
									opponentTotalLinesCleared += cleared;
									ShowDisplayInfo();
									RefreshScreen();
								}
							}
							break;
						case NP_insertJunk:
						{
							netint2 data[2];

							memcpy(data, event.u.net.data, sizeof(data));
							InsertJunk(scr2, ntoh2(data[0]), ntoh2(data[1]));
							break;
						}
						case NP_pause:
						{
							netint2 data[1];

							memcpy(data, event.u.net.data, sizeof(data));
							pausedByThem = ntoh2(data[0]);
							paused = pausedByMe || pausedByThem;
							if (robotEnable)
								RobotCmd(1, "Pause %d %d\n", pausedByMe,
									pausedByThem);
							ShowPause(pausedByMe, pausedByThem);
							changed = 1;
							break;
						}
						default:
							break;
					}
					break;
				case E_lostRobot:
				case E_lostConn:
					wonLast = 1;
					goto gameOver;
				default:
					break;
			}
			if (paused != oldPaused) {
				if (paused)
					pauseTimeLeft = SetITimer(0, 0);
				else
					SetITimer(speed, pauseTimeLeft);
				oldPaused = paused;
			}
		}
	nextPiece:
		dropMode = 0;
		FreezePiece(scr);
		myLinesCleared += linesCleared = ClearFullLines(scr);
		myTotalLinesCleared += linesCleared;
		if (linesCleared) {
			ShowDisplayInfo();
			RefreshScreen();
		}
		if (linesCleared > 0 && spied)
			SendPacket(NP_clear, 0, NULL);
		if (gameType == GT_classicTwo && linesCleared > 1) {
			short junkLines;
			netint2 data[1];

			junkLines = linesCleared - (linesCleared < 4);
			data[0] = hton2(junkLines);
			SendPacket(NP_giveJunk, sizeof(data), data);
		}
	}
	wonLast = 0;

gameOver:
	SetITimer(0, 0);
}

ExtFunc int main(int argc, char **argv)
{
	int ch, done = 0;
	char *hostStr = NULL, *portStr = NULL;
	MyEvent event;
	netType = NET_INVALID;

	standoutEnable = colorEnable = 1;
	stepDownInterval = DEFAULT_INTERVAL;
	MapKeys(DEFAULT_KEYS);
	while ((ch = getopt(argc, argv, "hHRs:r:Fk:c:woDSCp:i:")) != -1)
		switch (ch) {
			case 'c':
				netType = NET_CLIENT;
				hostStr = optarg;
				break;
			case 'w':
				netType = NET_SERVER;
				break;
			case 'p':
				portStr = optarg;
				break;
			case 'i':
				stepDownInterval = atof(optarg) * 1e6;
				break;
			case 's':
				initSeed = atoi(optarg);
				myFlags |= SCF_setSeed;
				break;
			case 'r':
				robotEnable = 1;
				robotProg = optarg;
				myFlags |= SCF_usingRobot;
				break;
			case 'F':
				fairRobot = 1;
				myFlags |= SCF_fairRobot;
				break;
			case 'D':
				dropModeEnable = 1;
				break;
			case 'C':
				colorEnable = 0;
				break;
			case 'S':
				standoutEnable = 0;
				break;
			case 'k':
				MapKeys(optarg);
				break;
			case 'H':
				DistInfo();
				exit(0);
			case 'R':
				Rules();
				exit(0);
			case 'h':
				Usage();
				exit(0);
			default:
				Usage();
				exit(1);
		}
	if (optind < argc) {
		Usage();
		exit(1);
	}
	if (fairRobot && !robotEnable)
		fatal("You can't use the -F option without the -r option");

	InitUtil();
	InitScreens();
	while(!done) {
		if (robotEnable)
			InitRobot(robotProg);
		InitNet();
		if (!initSeed)
			SRandom(time(0));
		if (netType != NET_INVALID) {
			gameType = GT_classicTwo;
			InitBoard(0);
			InitBoard(1);
			PrintStatus(netType == NET_CLIENT
						? "Connecting to opponent..."
						: "Waiting for opponent..."); 
			ShowDisplayInfo();
			RefreshScreen();
			if (netType == NET_CLIENT)
				InitiateConnection(hostStr, portStr);
			else if (netType == NET_SERVER)
				WaitForConnection(portStr);
			ClearStatus();
			{
				netint4 data[2];
				int major;

				data[0] = hton4(MAJOR_VERSION);
				data[1] = hton4(PROTOCOL_VERSION);
				SendPacket(NP_version, sizeof(data), data);
				if (WaitMyEvent(&event, EM_net) != E_net)
					fatal("Network negotiation failed");
				memcpy(data, event.u.net.data, sizeof(data));
				major = ntoh4(data[0]);
				protocolVersion = ntoh4(data[1]);
				if (event.u.net.type != NP_version || major < MAJOR_VERSION)
					fatal("Your opponent is using an old, incompatible version\n"
						  "of Netris.  They should get the latest version.");
				if (major > MAJOR_VERSION)
					fatal("Your opponent is using an newer, incompatible version\n"
						  "of Netris.  Get the latest version.");
				if (protocolVersion > PROTOCOL_VERSION)
					protocolVersion = PROTOCOL_VERSION;
			}
			if (protocolVersion < 3 && stepDownInterval != DEFAULT_INTERVAL)
				fatal("Your opponent's version of Netris predates the -i option.\n"
					  "For fairness, you shouldn't use the -i option either.");
			{
				netint4 data[3];
				int len;
				int seed;

				if (protocolVersion >= 3)
					len = sizeof(data);
				else
					len = sizeof(netint4[2]);
				if ((myFlags & SCF_setSeed))
					seed = initSeed;
				else
					seed = time(0);
				if (netType == NET_SERVER)
					SRandom(seed);
				data[0] = hton4(myFlags);
				data[1] = hton4(seed);
				data[2] = hton4(stepDownInterval);
				SendPacket(NP_startConn, len, data);
				if (WaitMyEvent(&event, EM_net) != E_net ||
					event.u.net.type != NP_startConn)
					fatal("Network negotiation failed");
				memcpy(data, event.u.net.data, len);
				opponentFlags = ntoh4(data[0]);
				seed = ntoh4(data[1]);
				if (netType == NET_CLIENT) {
					if ((opponentFlags & SCF_setSeed) != (myFlags & SCF_setSeed))
						fatal("If one player sets the random number seed, "
							  "both must.");
					if ((myFlags & SCF_setSeed) && seed != initSeed)
						fatal("Both players have set the random number seed, "
							  "and they are unequal.");
					if (protocolVersion >= 3 && stepDownInterval != ntoh4(data[2]))
						fatal("Your opponent is using a different step-down "
							  "interval (-i).\nYou must both use the same one.");
					SRandom(seed);
				}
			}
			{
				char *userName;
				int len, i;

				userName = getenv("LOGNAME");
				if (!userName || !userName[0])
					userName = getenv("USER");
				if (!userName || !userName[0])
					strcpy(userName, "???");
				len = strlen(userName)+1;
				if (len > sizeof(opponentName))
					len = sizeof(opponentName);
				SendPacket(NP_userName, len, userName);
				if (WaitMyEvent(&event, EM_net) != E_net ||
					event.u.net.type != NP_userName)
					fatal("Network negotiation failed");
				strncpy(opponentName, event.u.net.data,
					sizeof(opponentName)-1);
				opponentName[sizeof(opponentName)-1] = 0;
				for (i = 0; opponentName[i]; ++i)
					if (!isprint(opponentName[i]))
						opponentName[i] = '?';
				for (i = 0; opponentHost[i]; ++i)
					if (!isprint(opponentHost[i]))
						opponentHost[i] = '?';
			}
			OneGame(0, 1);
			InvertScreen(0);
			InvertScreen(1);
		}
		else {
			gameType = GT_onePlayer;
			OneGame(0, -1);
			InvertScreen(0);
			RefreshScreen();
		}

		if (wonLast) {
			won++;
		} else {
			lost++;
			if(gameType != GT_onePlayer) {
				// FIXME: A race condition occurrs here IF THERE IS A DRAW,
				// resulting in both sides waiting forever.
				WaitMyEvent(&event, EM_net);
			}
		}
		CloseNet();

		if (robotEnable) {
			CloseRobot();
		} else {
			ShowDisplayInfo();
			PrintStatus("Press '%c' for a new game.", keyTable[KT_new]);
			RefreshScreen();
			for(;;) {
				int key = getchar();
				if(key == keyTable[KT_new])
					break;
				if(key == keyTable[KT_quit])
					exit(0);
			}
		}
	}

	return 0;
}

/*
 * vi: ts=4 ai
 * vim: noai si
 */
