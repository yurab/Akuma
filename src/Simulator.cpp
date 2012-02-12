#include "Simulator.h"
#include <iostream>
#include <string>
#include <aku/AKU.h>
#include <aku/AKU-luaext.h>
#include <aku/AKU-untz.h>
#include "Utils.h"
#include "Input.h"
#include <FileWatcher/FileWatcher.h>
#include <SDL.h>
#include <SDL_opengl.h>

using namespace std;
namespace fs = boost::filesystem;

namespace
{
	AKUContextID akuContext;
	FW::FileWatcher fw;
	Uint32 frameDelta = 1000 / 60;
}

struct ProjectFolderWatchListener: public FW::FileWatchListener
{
	void handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action)
	{
		dirty = true;
	}

	bool dirty;
};

ProjectFolderWatchListener projListener;

void enterFullscreenMode()
{
	cout << "Fullscreen mode is not supported" << endl;
}

void exitFullscreenMode()
{
	cout << "Fullscreen mode is not supported" << endl;
}

void openWindow(const char* title, int width, int height)
{
	SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO);
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	SDL_SetVideoMode(width, height, 32, SDL_OPENGL);
	AKUDetectGfxContext();
	AKUSetScreenSize(width, height);
}

void closeWindow()
{
	AKUReleaseGfxContext();
	SDL_Quit();
}

ExitReason::Enum startGameLoop()
{
	Uint32 lastFrame = SDL_GetTicks();
	while(true)
	{
		SDL_Event ev;
		while(SDL_PollEvent(&ev))
		{
			switch(ev.type)
			{
			case SDL_QUIT:
				closeWindow();
				return ExitReason::UserAction;
			case SDL_KEYDOWN:
				if(ev.key.keysym.sym == SDLK_r && (SDL_GetModState() & KMOD_CTRL))
				{
					closeWindow();
					return ExitReason::Restart;
				}
				break;
			}

			injectInput(ev);
		}

		fw.update();
		if(!projListener.dirty)
		{
			AKUUpdate();
			glClear(GL_COLOR_BUFFER_BIT);
			AKURender();
			SDL_GL_SwapBuffers();

			Uint32 currentFrame = SDL_GetTicks();
			Uint32 delta = currentFrame - lastFrame;
			if(delta < frameDelta)
				SDL_Delay(frameDelta - delta);
			lastFrame = SDL_GetTicks();

			continue;
		}
		else
		{
			cout << "Change to project folder detected" << endl;
			while(true)//delay reload in case of a lot of changes
			{
				projListener.dirty = false;
				SDL_Delay(100);
				fw.update();
				if(!projListener.dirty)
					break;
			}
			return ExitReason::Restart;
		}
	}
}

bool initSimulator(const fs::path& profilePath, const char* profile)
{
	writeSeparator();
	cout << "Initializing AKU" << endl;

	akuContext = AKUCreateContext();
	//Load extensions
	AKUExtLoadLuacrypto();
	AKUExtLoadLuacurl();
	AKUExtLoadLuasocket();
	AKUExtLoadLuasql();
	//Load untz
	AKUUntzInit();

	initInput();

	AKUSetFunc_EnterFullscreenMode(&enterFullscreenMode);
	AKUSetFunc_ExitFullscreenMode(&exitFullscreenMode);
	AKUSetFunc_OpenWindow(&openWindow);

	//Load base script
	AKURunScript((profilePath / "Akuma.lua").string().c_str());
	cout << "Loading profile " << profile << endl;
	AKURunScript((profilePath / (string(profile) + ".lua")).string().c_str());

	writeSeparator();

	return true;
}

ExitReason::Enum startSimulator(const boost::filesystem::path& pathToMain)
{
	fs::path filename = pathToMain.filename();
	fs::path projectDir = pathToMain.parent_path();
	//change working directory
	AKUSetWorkingDirectory(projectDir.string().c_str());
	//Start the watcher
	projListener.dirty = false;
	FW::WatchID watchID = fw.addWatch(projectDir.string(), &projListener);
	//run the main script
	AKURunScript(filename.string().c_str());
	ExitReason::Enum exitReason = startGameLoop();

	fw.removeWatch(watchID);
	//Ensure that window is closed
	closeWindow();
	AKUDeleteContext(akuContext);

	return exitReason;
}