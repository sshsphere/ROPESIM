#include "raylib-cpp.hpp"
#include <iostream> 
#include <string>
#include <list>
#include <glm/glm.hpp>
#include <cassert>
#include <map>
#include <ranges>
//Find interpolated position between two points
raylib::Vector2 InterpPos(const raylib::Vector2& curpos, const raylib::Vector2& nextpos, double interp) {
	return curpos + (nextpos - curpos) * interp;
}
//Keep track of ids of objects
struct IDGen {
private:
	static inline int id{};
public:
	static int getId() {
		return id++;
	}
	static int nextId() {
		return id;
	}
};

struct Point {
	raylib::Vector2 pos{}, prevpos{};
	bool isLocked{};
	int id{ IDGen::getId() };
	Point(const raylib::Vector2& _pos, const raylib::Vector2& _prevpos, bool _isLocked)
		:pos{ _pos }, prevpos{ _prevpos }, isLocked{ _isLocked } {}
	Point() = default;
	raylib::Vector2 InterpPos(const Point& next, double interp) {
		return ::InterpPos(pos, next.pos, interp);
	}
	void Draw(Point& next, double interp) {
		raylib::Color col{ WHITE };
		if (isLocked) {
			col = RED;
		}
		InterpPos(next, interp).DrawCircle(2.0f, col); // Draw circle by interpolating between two positions
	}
};
//Use interpolated position for collision checking with UI elements only
struct Stick {
	Point* pointA;
	Point* pointB;
	float length{};
	int id{ IDGen::getId() };
	Stick(Point& point1, Point& point2) :pointA{ &point1 }, pointB{ &point2 }
	{
		length = (pointA->pos - pointB->pos).Length();
	}
	void Draw(const Stick& next, double interp) {
		pointA->InterpPos(*next.pointA, interp).DrawLine(pointB->InterpPos(*next.pointB, interp), 1.0f, WHITE);
	}
};
struct tempLine {
	raylib::Vector2 startPos{};
	raylib::Vector2 endPos{};
	bool active{};
	void Draw() {
		if (active) {
			startPos.DrawLine(endPos, 1.0f, GRAY);
		}
	}
};
struct GameContext {
	std::map<int, Point> points;
	std::vector<Stick> sticks;
	void deepCopy(const GameContext& context) {
		points = context.points;
		sticks = context.sticks;
		for (auto& p : sticks) {
			p.pointA = &points[p.pointA->id];
			p.pointB = &points[p.pointB->id];
		}
	}
	GameContext() = default;
	GameContext(const GameContext& context) {
		deepCopy(context);
	}
	GameContext& operator=(const GameContext& context) {
		deepCopy(context);
		return *this;
	}
};
//Game context that does not need to be interpolated
struct StaticContext {
	bool isActive{};
	tempLine templine;
};
void Simulate(GameContext& context, const StaticContext& scontext, double deltatime) {
	static constexpr double gravity = 100.0;
	static constexpr int numOfIterations = 5; //Number of iterations for the physics simulation, the more iterations, the more precise the simulation gets
	if (!scontext.isActive)return;
	for (auto& p : context.points | std::views::values) {
		if (!p.isLocked) {
			//Apply gravity to unlocked points
			raylib::Vector2 posBeforeUpdate = p.pos;
			p.pos += p.pos - p.prevpos;
			p.pos.y += gravity * deltatime * deltatime;
			p.prevpos = posBeforeUpdate;
		}
	}
	for (int i = 0; i < numOfIterations; ++i) {
		for (auto& p : context.sticks) {
			//Simulate connected points
			raylib::Vector2 stickCentre = (p.pointA->pos + p.pointB->pos) / 2;
			raylib::Vector2 stickDir = (p.pointA->pos - p.pointB->pos).Normalize();
			if (!p.pointA->isLocked)
				p.pointA->pos = stickCentre + stickDir * p.length / 2.0f;
			if (!p.pointB->isLocked)
				p.pointB->pos = stickCentre - stickDir * p.length / 2.0f;
		}
	}

}
void drawPoints(std::map<int, Point>& points, std::map<int, Point>& newpoints, double interp) {
	auto curp = points.begin();
	auto newp = newpoints.begin();
	for (; curp != points.end() && newp != newpoints.end(); ++curp, ++newp)
	{
		if (curp->second.id != newp->second.id) {
			curp++;
		}
		curp->second.Draw(newp->second, interp);
	}
}
void drawSticks(std::vector<Stick>& sticks, std::vector<Stick>& newsticks, double interp) {
	auto curp = sticks.begin();
	auto newp = newsticks.begin();
	for (; curp != sticks.end() && newp != newsticks.end(); ++curp, ++newp)
	{
		while (curp->id != newp->id) {
			curp++;
		}
		curp->Draw(*newp, interp);
	}
}
void checkStaticInput(GameContext& nextContext, StaticContext& staticContext, raylib::Camera2D& cam) {
	if (IsKeyPressed(KEY_SPACE))staticContext.isActive ^= 1;
	static bool isDragging{ false };
	static Point* dragOrigin{};
	auto targetPoint{ nextContext.points.end() };
	raylib::Vector2 pos{ cam.GetScreenToWorld(GetMousePosition()) };
	for (auto p{ nextContext.points.begin() }; p != nextContext.points.end(); ++p) {
		if (CheckCollisionPointCircle(pos, p->second.pos, 2.0f)) {
			targetPoint = p;
			break;
		}
	}
	bool isColliding = (targetPoint != nextContext.points.end());
	if (raylib::Mouse::IsButtonPressed(MOUSE_BUTTON_RIGHT) && isColliding) {
		auto it = nextContext.sticks.begin();
		while (it != nextContext.sticks.end()) {
			if (it->pointA->pos == targetPoint->second.pos || it->pointB->pos == targetPoint->second.pos) {
				it = nextContext.sticks.erase(it);
			}
			else {
				++it;
			}
		}
		nextContext.points.erase(targetPoint);
	}
	if (staticContext.isActive) {
		isDragging = false;
		staticContext.templine.active = false;
		return;
	}
	if (isDragging) {
		staticContext.templine.endPos = pos;
		if (!raylib::Mouse::IsButtonDown(MOUSE_BUTTON_LEFT)) {
			isDragging = false;
			staticContext.templine.active = false;
			if (isColliding && (targetPoint->second.pos != dragOrigin->pos) &&
				std::ranges::find_if(nextContext.sticks,
					[&](const Stick& stick) //Avoid creating multiple sticks that connect the same two points
					{
						return (stick.pointA->id == dragOrigin->id && stick.pointB->id == targetPoint->second.id) ||
							(stick.pointA->id == targetPoint->second.id && stick.pointB->id == dragOrigin->id);
					}) == nextContext.sticks.end())
			{
				nextContext.sticks.push_back({ *dragOrigin,targetPoint->second });
			}
		}
		return;
	}
	if (raylib::Mouse::IsButtonPressed(MOUSE_BUTTON_LEFT) && isColliding) {
		isDragging = true;
		dragOrigin = &(targetPoint->second);
		staticContext.templine.active = true;
		staticContext.templine.startPos = dragOrigin->pos;
		staticContext.templine.endPos = dragOrigin->pos;
		return;
	}
	if (raylib::Mouse::IsButtonPressed(MOUSE_BUTTON_LEFT)) {
		nextContext.points.insert({ IDGen::nextId(), {pos, pos, 0} });
		return;
	}
	if (raylib::Mouse::IsButtonPressed(MOUSE_BUTTON_MIDDLE) && isColliding) {
		targetPoint->second.isLocked ^= 1;
		return;
	}
}
class Game {
private:
	GameContext curContext{};
	GameContext nextContext{};
	StaticContext staticContext{};
	static constexpr int tps{ 60 }; //ticks per seconds - simulation "framerate"
	static constexpr double timeskip{ 1.0 / tps };
	static constexpr int maxframeskip{ 5 };
	static constexpr raylib::Vector2 startSize{ 800.0f,450.0f };
	static constexpr float render_scale{ 0.01f };
	double interp{};
	double updateTime{};

	raylib::Window window;
	raylib::AudioDevice audioDevice;
	raylib::Camera2D cam;

	void processControlInput() {
		static raylib::Vector2 previousSize = startSize;
		if (IsKeyPressed(KEY_F11)) {
			if (window.IsFullscreen()) {
				window.SetFullscreen(false);
				window.SetSize(previousSize);
			}
			else {
				previousSize = window.GetSize();
				window.SetSize(GetMonitorWidth(GetCurrentMonitor()), GetMonitorHeight(GetCurrentMonitor()));
				window.SetFullscreen(true);
			}
		}
	}
	void updateCameraPosition() {
		if (!window.IsFullscreen()) {
			cam.SetOffset({ 0.5f * window.GetWidth(),0.5f * window.GetHeight() });
			cam.SetZoom(window.GetHeight() * render_scale);
		}
		else {
			cam.SetOffset({ 0.5f * GetMonitorWidth(GetCurrentMonitor()),0.5f * GetMonitorHeight(GetCurrentMonitor()) });
			cam.SetZoom(GetMonitorHeight(GetCurrentMonitor()) * render_scale);
		}
	}

	void drawGame() {
		//Draw in relative camera coordinates 
		cam.BeginMode();
		{
			drawSticks(curContext.sticks, nextContext.sticks, interp);
			drawPoints(curContext.points, nextContext.points, interp);
			staticContext.templine.Draw();
		}
		cam.EndMode();
	}
public:
	Game()
		:window{ startSize.x, startSize.y, "Rope sim" }, audioDevice{}
	{
		SetConfigFlags(FLAG_WINDOW_RESIZABLE);
		cam.SetTarget({ 0,0 });
		cam.SetOffset(startSize);
		cam.SetRotation(0.0f);
		cam.SetZoom(1.0f);
		updateTime = GetTime();
	}
	void run() {
		//Saves the previous game state and then interpolates between the two
		while (!window.ShouldClose()) {
			processControlInput();
			updateCameraPosition();
			checkStaticInput(nextContext, staticContext, cam);
			int loops = 0;
			while (GetTime() > updateTime && loops < maxframeskip) {
				curContext = nextContext;
				Simulate(nextContext, staticContext, timeskip);
				updateTime += timeskip;
				loops++;
			}
			interp = (GetTime() + timeskip - updateTime) / timeskip;
			BeginDrawing();
			window.ClearBackground(SKYBLUE);
			drawGame();
			DrawFPS(10, 10);
			EndDrawing();
		}
	}
};
int main() {

	Game game{};
	game.run();
}
