#include <SDL2/SDL.h>
#include <iostream>
#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>
#include <fstream>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <array>

constexpr int PORT = 7693;
constexpr int BUFFERSIZE = 1024;
constexpr int PIXELSIZE = 12;
constexpr int SCREENWIDTH = 57;
constexpr int SCREENHEIGHT = 14;

void pauseunpause();
void resettimer();
void inputhandler(SDL_Keysym key, Uint32 state);
std::mutex progaccess;

std::atomic<bool> sdlquit = false;
SDL_Window* window = NULL;
SDL_Surface* winsurface = NULL;

std::string savedir = "";

void SDLPutPixel(int x, int y, SDL_Color c)
{
    if(x>=0 && x<SCREENWIDTH && y>=0 && y<SCREENHEIGHT)
    {
        SDL_Rect p = {x*PIXELSIZE,y*PIXELSIZE,PIXELSIZE,PIXELSIZE};
        SDL_FillRect(winsurface,&p,SDL_MapRGB(winsurface->format,c.r,c.g,c.b));
    }
}

void ipcmain()
{
    int sockfd;
    char buffer[BUFFERSIZE];
    sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    if ((sockfd=socket(AF_INET,SOCK_DGRAM,0))<0)
    {
        std::cout << "Couldn't initialize socket\n";
        return;
    }

    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if(bind(sockfd,(const sockaddr*)&server_addr,sizeof(server_addr))<0)
    {
        std::cout << "Couldn't bind socket\n";
        close(sockfd);
        return;
    }


    std::cout << "Listening on port " << PORT << "\n";

    while(1)
    {
        int n = recvfrom(sockfd,(char*)buffer,BUFFERSIZE,0,(sockaddr*)&client_addr,&client_len);
        if(n<0)
        {
            std::cout << "Recvfrom failed\n";
            break;
        }
        buffer[n] = '\0';
        std::cout << "Received " << buffer << "\n";
        int pcount = 0,rcount = 0;
        int thelength = std::min((int)strlen(buffer),8);
        for(int i=0;i<thelength;i++)
        {
            if(buffer[i] == 'P')
            {
                pcount++;
            }
            else if(buffer[i] == 'R')
            {
                rcount++;
            }
        }
        if(pcount>4)
        {
            progaccess.lock();
            pauseunpause();
            progaccess.unlock();
        }
        else if(rcount>4)
        {
            progaccess.lock();
            resettimer();
            progaccess.unlock();
        }
    }
    close(sockfd);
}


int pressednum = -1;
int presseddir = 0;

int heldnum = -1;
int helddir = 0;

int modifiers = 0;
enum
{
	MOD_SHIFT = 1,
	MOD_CTRL = 2
};

int confirmnum = -1;

bool segmentpixels[7][7][4] =
{
    {
        {1,1,1,1},
        {0,0,0,0},
        {0,0,0,0},
        {0,0,0,0},
        {0,0,0,0},
        {0,0,0,0},
        {0,0,0,0},
    },
    {
        {1,0,0,0},
        {1,0,0,0},
        {1,0,0,0},
        {1,0,0,0},
        {0,0,0,0},
        {0,0,0,0},
        {0,0,0,0},
    },
    {
        {0,0,0,1},
        {0,0,0,1},
        {0,0,0,1},
        {0,0,0,1},
        {0,0,0,0},
        {0,0,0,0},
        {0,0,0,0},
    },
    {
        {0,0,0,0},
        {0,0,0,0},
        {0,0,0,0},
        {1,1,1,1},
        {0,0,0,0},
        {0,0,0,0},
        {0,0,0,0},
    },
    {
        {0,0,0,0},
        {0,0,0,0},
        {0,0,0,0},
        {1,0,0,0},
        {1,0,0,0},
        {1,0,0,0},
        {1,0,0,0},
    },
    {
        {0,0,0,0},
        {0,0,0,0},
        {0,0,0,0},
        {0,0,0,1},
        {0,0,0,1},
        {0,0,0,1},
        {0,0,0,1},
    },
    {
        {0,0,0,0},
        {0,0,0,0},
        {0,0,0,0},
        {0,0,0,0},
        {0,0,0,0},
        {0,0,0,0},
        {1,1,1,1},
    },
};

bool segmentsenabled[10][7] =
{
    {1,1,1,0,1,1,1},
    {0,0,1,0,0,1,0},
    {1,0,1,1,1,0,1},
    {1,0,1,1,0,1,1},
    {0,1,1,1,0,1,0},
    {1,1,0,1,0,1,1},
    {1,1,0,1,1,1,1},
    {1,0,1,0,0,1,0},
    {1,1,1,1,1,1,1},
    {1,1,1,1,0,1,1},
};

const int digitx[9] = {2,7,12,20,25,33,38,46,51};

int timerstate = 0;
int curdigit = 0;


bool pressedstate = false;
bool heldstate = false;
bool escstate = false;

using sysclock = std::chrono::system_clock;

sysclock::time_point starttime,endtime;
uint64_t clockoffset = 0;

char clockdigits[9] = {0,0,0,0,0,0,0,0,0};
const int timediv[9] = {10,10,10,6,10,6,10,10,10};

SDL_Color c1 = SDL_Color(255,255,255,255);
SDL_Color c2 = SDL_Color(0,0,0,255);


void inttodigits(uint64_t cs)
{
    for(int i=8;i>=0;i--)
    {
        clockdigits[i] = cs%timediv[i];
        cs /= timediv[i];
    }
}

uint64_t digitstoint()
{
    int factor = 1;
    uint64_t retval = 0;
    for(int i=8;i>=0;i--)
    {
        retval += clockdigits[i]*factor;
        factor *= timediv[i];
    }
    return retval;
}


void drawnum(int num, int x, int y)
{
    for(int seg=0;seg<7;seg++)
    {
        if(segmentsenabled[num][seg])
        {
            for(int ny=0;ny<7;ny++)
            {
                for(int nx=0;nx<4;nx++)
                {
                    if(segmentpixels[seg][ny][nx])
                    {
                        SDLPutPixel(x+nx,y+ny,c1);
                    }
                }
            }
        }
    }
}

const auto statefiles = std::to_array<std::string>({
	"state1.clk",
	"state2.clk",
	"state3.clk",
	"state4.clk",
	"state5.clk",
	"state6.clk",
	"state7.clk",
	"state8.clk",
	"state9.clk",
	"state0.clk"
}
);

void saveclock(unsigned i)
{
	i--;
	if(i<10)
	{
		std::cout << "Saving " << i << "\n";
		auto statefile = savedir+statefiles[i];
		std::filebuf cf;
		cf.open(statefile,std::ios::out | std::ios::binary);
		std::ostream clockfile(&cf);
		clockfile.write(clockdigits,9);
		cf.close();
	}
}

void loadclock(unsigned i)
{
	i--;
	if(i<10)
	{
		std::cout << "Loading " << i << "\n";
		auto statefile = savedir+statefiles[i];
		std::filebuf cf;
		cf.open(statefile,std::ios::in | std::ios::binary);
		std::istream clockfile(&cf);
		char tempclockdigits[9] = {9,9,9,9,9,9,9,9,9};
		//std::cout << "Before: "; for(int i=0;i<9;i++) std::cout << static_cast<int>(tempclockdigits[i]) << ""; std::cout << "\n";
		clockfile.read(tempclockdigits,9);
		//std::cout << "After: "; for(int i=0;i<9;i++) std::cout << static_cast<int>(tempclockdigits[i]) << ""; std::cout << "\n";
		cf.close();
		memcpy(clockdigits,tempclockdigits,9);
	}
}

void pauseunpause()
{
    if(timerstate==1)
    {
        timerstate = 0;
    }
    else
    {
        timerstate = 1;
        starttime = sysclock::now();
        clockoffset = digitstoint();
    }
}

void resettimer()
{
    for(int i=0;i<9;i++)
    {
        clockdigits[i]=0;
    }
    timerstate = 0;
}

void timerdisplay()
{
    SDL_Event e;

    while(!sdlquit)
    {
		c1 = (timerstate == 2) ? 
						SDL_Color(0,0,0,255) :
						SDL_Color(255,255,255,255);
		c2 = (timerstate == 2) ? 
						SDL_Color(255,255,255,255) :
						SDL_Color(0,0,0,255);
        SDL_FillRect(winsurface,NULL,std::bit_cast<Uint32>(c2));
        SDLPutPixel(17,3,c1);
        SDLPutPixel(17,8,c1);
        SDLPutPixel(30,3,c1);
        SDLPutPixel(30,8,c1);
        SDLPutPixel(43,8,c1);
        progaccess.lock();
        while(SDL_PollEvent(&e))
        {
            if(e.type == SDL_QUIT)
            {
                sdlquit = true;
            }
            else if(e.type == SDL_KEYDOWN || e.type == SDL_KEYUP)
            {
                inputhandler(e.key.keysym,e.type);
            }

        }

        switch(timerstate)
        {
            default:
            case 0:
                {
                    curdigit += presseddir;
                    while(curdigit<0)
                        curdigit += 9;
                    curdigit %= 9;
                    for(int x=0;x<4;x++)
                    {
                        SDLPutPixel(digitx[curdigit]+x,11,{255,255,255,255});
                    }
					if(!modifiers)
					{
						if(pressednum >= 0)
							clockdigits[curdigit] = pressednum;
						if(pressedstate)
						{
							timerstate = 1;
							starttime = sysclock::now();
							clockoffset = digitstoint();
						}
					}
					else if(modifiers == MOD_CTRL)
					{
						if(pressednum >= 0)
							loadclock(pressednum);
					}
					else if(modifiers == MOD_CTRL | MOD_SHIFT)
					{
						if(pressednum >= 0)
						{
							timerstate = 2;
							confirmnum = pressednum;
						}
					}
                    //std::cout << presseddir << " " << helddir << " " << curdigit << " " << pressednum << "\n";
                }
                break;
            case 1:
                {
                    endtime = sysclock::now();
                    std::chrono::duration<double> offsettime = endtime-starttime;
                    uint64_t cs = std::chrono::duration_cast<std::chrono::milliseconds>(offsettime).count()/10;
                    inttodigits(cs+clockoffset);
                    if(pressedstate)
                        timerstate = 0;
                }
                break;
			case 2:
				{
					if(pressednum == confirmnum)
					{
						if(modifiers == MOD_CTRL | MOD_SHIFT)
						{
							saveclock(pressednum);
							timerstate = 0;
						}
					}
					else if(escstate)
					{
						timerstate = 0;
					}
				}
				break;
        }

        progaccess.unlock();

        for(int i=0;i<9;i++)
        {
            drawnum(clockdigits[i],digitx[i],2);
        }

        presseddir = 0;
		pressednum = -1;
        pressedstate = false;
		escstate = false;

        SDL_UpdateWindowSurface(window);
    }
}

void inputhandler(SDL_Keysym key, Uint32 state)
{
    if(state == SDL_KEYDOWN)
    {
        if(key.sym >= SDLK_0 && key.sym <= SDLK_9)
        {
            pressednum = key.sym-SDLK_0;
            return;
        }
        switch(key.sym)
        {
            case SDLK_l:
                pressedstate = true;
                break;
            case SDLK_m:
                resettimer();
				break;
            case SDLK_LEFT:
                presseddir = -1;
                helddir = -1;
                break;
            case SDLK_RIGHT:
                presseddir = 1;
                helddir = 1;
                break;
			case SDLK_LCTRL:
				modifiers |= MOD_CTRL;
				break;
			case SDLK_LSHIFT:
				modifiers |= MOD_SHIFT;
				break;
			case SDLK_ESCAPE:
				escstate = true;
				break;
        }
    }
    else
    {
        switch(key.sym)
        {
            case SDLK_LEFT:
            case SDLK_RIGHT:
                helddir = 0;
                break;
			case SDLK_LCTRL:
				modifiers &= ~MOD_CTRL;
				break;
			case SDLK_LSHIFT:
				modifiers &= ~MOD_SHIFT;
				break;
        }
    }
}

int main(int argc,char** argv)
{
	for(int i=1;i<argc;i++)
	{
		if(!strcmp("-dir",argv[i]))
		{
			i++;
			if(i>=argc)
			{
				std::cout << "Invalid argument." << std::endl;
				return -1;
			}
			savedir = argv[i];
			if(!savedir.ends_with('/'))
			{
				savedir+='/';
			}
		}
		else
		{
			std::cout << "Invalid option." << std::endl;
			return -1;
		}
	}
    if(SDL_Init(SDL_INIT_VIDEO)<0)
    {
        std::cout << "Couldn't initialize SDL: " << SDL_GetError() << std::endl;
        return -1;
    }
    window = SDL_CreateWindow("Nambona Timer",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,SCREENWIDTH*PIXELSIZE,SCREENHEIGHT*PIXELSIZE,SDL_WINDOW_SHOWN);
    if(window==NULL)
    {
        std::cout << "Couldn't create window: " << SDL_GetError() << std::endl;
        return -1;
    }
    winsurface = SDL_GetWindowSurface(window);
    std::thread ipcthread(ipcmain);
    timerdisplay();
    SDL_Quit();
}