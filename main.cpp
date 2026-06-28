#include <cmath>
#include <cstdio>
#include <iostream>
#include <ctgmath>
#include <string>
#include <vector>

#include "pugixml.hpp"
#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include "raymath.h"

#include "dialogs.h"

constexpr float HANDLE_SIZE = 18.0f;

bool stringEndsWith (std::string const &fullString, std::string const &ending) {
	if (fullString.length() >= ending.length()) {
		return (0 == fullString.compare (fullString.length() - ending.length(), ending.length(), ending));
	}
	return false;
}

struct FrameData {
	float frameX, frameY, frameWidth, frameHeight;
};

bool loadAtlas(Texture &spritesheet, Rectangle &imageDimensions, pugi::xml_document &doc,
               std::vector<Rectangle> &frameRects, std::vector<Rectangle> &originalFrameRects,
               std::vector<FrameData> &originalFrameData, std::vector<std::string> &frameNames,
               Rectangle &container, const std::string &pngPath) {
	UnloadTexture(spritesheet);

	spritesheet = LoadTexture(pngPath.c_str());
	SetTextureFilter(spritesheet, TEXTURE_FILTER_BILINEAR);
	imageDimensions = Rectangle{.x = 0, .y = 0, .width = static_cast<float>(spritesheet.width), .height = static_cast<float>(spritesheet.height)};

	std::string xmlPath = pngPath;
	const pugi::xml_parse_result result = doc.load_file(xmlPath.replace(xmlPath.length() - 3, 3, "xml").c_str());

	frameRects.clear();
	originalFrameRects.clear();
	originalFrameData.clear();
	frameNames.clear();

	if (!result) {
		std::cerr << "Error loading XML: " << result.description() << std::endl;
		return false;
	}

	for (auto frame : doc.child("TextureAtlas").children("SubTexture")) {
		const float x = frame.attribute("x").as_float();
		const float y = frame.attribute("y").as_float();
		const float width = frame.attribute("width").as_float();
		const float height = frame.attribute("height").as_float();

		frameRects.push_back(Rectangle{.x = x, .y = y, .width = width, .height = height});
		originalFrameRects.push_back(Rectangle{.x = x, .y = y, .width = width, .height = height});
		originalFrameData.push_back(FrameData{
			frame.attribute("frameX").as_float(),
			frame.attribute("frameY").as_float(),
			frame.attribute("frameWidth").as_float(),
			frame.attribute("frameHeight").as_float()
		});
		frameNames.push_back(frame.attribute("name").as_string());
	}

	container = Rectangle{.x = 0, .y = 0, .width = imageDimensions.width, .height = imageDimensions.height};
	return true;
}

void saveOutput(const Texture &spritesheet, const pugi::xml_document &doc, const std::string &pngPath,
                const std::string &xmlPath, float scale, const Rectangle &container,
                const std::vector<Rectangle> &frameRects, const std::vector<Rectangle> &originalFrameRects,
                const std::vector<FrameData> &originalFrameData) {
	const int outW = static_cast<int>(std::round(container.width * scale));
	const int outH = static_cast<int>(std::round(container.height * scale));
	if (outW <= 0 || outH <= 0) return;

	Image sourceImg = LoadImageFromTexture(spritesheet);
	Image output = GenImageColor(outW, outH, BLANK);

	for (size_t i = 0; i < frameRects.size() && i < originalFrameRects.size(); ++i) {
		const Rectangle &src = originalFrameRects[i];
		const Rectangle &dst = frameRects[i];

		const int dx = static_cast<int>(std::round((dst.x - container.x) * scale));
		const int dy = static_cast<int>(std::round((dst.y - container.y) * scale));
		const int fw = static_cast<int>(std::round(src.width * scale));
		const int fh = static_cast<int>(std::round(src.height * scale));

		Rectangle srcRect = {.x = src.x, .y = src.y, .width = src.width, .height = src.height};
		Rectangle dstRect = {.x = static_cast<float>(dx), .y = static_cast<float>(dy), .width = static_cast<float>(fw), .height = static_cast<float>(fh)};

		ImageDraw(&output, sourceImg, srcRect, dstRect, WHITE);
	}

	ExportImage(output, pngPath.c_str());

	pugi::xml_document outDoc;
	pugi::xml_node outAtlas = outDoc.append_child("TextureAtlas");
	for (auto attr : doc.child("TextureAtlas").attributes()) {
		outAtlas.append_attribute(attr.name()).set_value(attr.value());
	}
	for (auto child : doc.child("TextureAtlas").children()) {
		outAtlas.append_copy(child);
	}

	outDoc.child("TextureAtlas").attribute("width").set_value(outW);
	outDoc.child("TextureAtlas").attribute("height").set_value(outH);

	auto frameIt = outDoc.child("TextureAtlas").children("SubTexture").begin();
	for (size_t i = 0; i < frameRects.size() && frameIt != outDoc.child("TextureAtlas").children("SubTexture").end(); ++i, ++frameIt) {
		const float newX = (frameRects[i].x - container.x) * scale;
		const float newY = (frameRects[i].y - container.y) * scale;
		const float newW = frameRects[i].width * scale;
		const float newH = frameRects[i].height * scale;

		frameIt->attribute("x").set_value(newX);
		frameIt->attribute("y").set_value(newY);
		frameIt->attribute("width").set_value(newW);
		frameIt->attribute("height").set_value(newH);

		if (i < originalFrameData.size() && frameIt->attribute("frameX")) {
			const float diffX = frameRects[i].x - originalFrameRects[i].x;
			const float diffY = frameRects[i].y - originalFrameRects[i].y;

			frameIt->attribute("frameX").set_value((originalFrameData[i].frameX + diffX) * scale);
			frameIt->attribute("frameY").set_value((originalFrameData[i].frameY + diffY) * scale);
			frameIt->attribute("frameWidth").set_value(originalFrameData[i].frameWidth * scale);
			frameIt->attribute("frameHeight").set_value(originalFrameData[i].frameHeight * scale);
		}
	}

	outDoc.save_file(xmlPath.c_str());
}

enum DragMode { DRAG_NONE, DRAG_CONTAINER_BODY, DRAG_CONTAINER_HANDLE, DRAG_FRAME };

int getContainerHandle(const Rectangle &container, Vector2 mouseImg, float threshold) {
	const float x = container.x, y = container.y, w = container.width, h = container.height;

	if (CheckCollisionPointRec(mouseImg, container)) {
		return -1;
	}

	if (CheckCollisionPointRec(mouseImg, Rectangle{.x = x - threshold, .y = y - threshold, .width = w + 2 * threshold, .height = h + 2 * threshold})) {
		if (mouseImg.x < x + threshold && mouseImg.y < y + threshold) return 0;
		if (mouseImg.x > x + w - threshold && mouseImg.y < y + threshold) return 1;
		if (mouseImg.x < x + threshold && mouseImg.y > y + h - threshold) return 2;
		if (mouseImg.x > x + w - threshold && mouseImg.y > y + h - threshold) return 3;
		if (mouseImg.x < x + threshold) return 4;
		if (mouseImg.x > x + w - threshold) return 5;
		if (mouseImg.y < y + threshold) return 6;
		if (mouseImg.y > y + h - threshold) return 7;
	}
	return -2;
}

int main() {

	Texture spritesheet{};
	Rectangle imageDimensions{};

	pugi::xml_document doc;
	InitWindow(1280, 720, "Sparrow Atlas Resizer");
	SetTargetFPS(GetMonitorRefreshRate(GetCurrentMonitor()));

	const Vector2 cameraTarget = Vector2Zero();

	auto camSpritesheet = Camera2D{.offset = Vector2Zero(), .target = cameraTarget, .rotation = 0.0f, .zoom = 1.0f};

	float scale = 1.0f;
	char scaleText[32] = "1.000";
	bool scaleEditMode = false;

	std::vector<Rectangle> frameRects = {};
	std::vector<Rectangle> originalFrameRects = {};
	std::vector<FrameData> originalFrameData = {};
	std::vector<std::string> frameNames = {};

	Rectangle container = {};
	bool editMode = false;
	int selectedFrame = -1;

	DragMode dragMode = DRAG_NONE;
	Vector2 dragStart = {};
	Rectangle dragContainerStart = {};
	Rectangle dragFrameStart = {};
	int dragHandle = -1;

	char frameXText[32] = "";
	char frameYText[32] = "";
	bool frameXEdit = false;
	bool frameYEdit = false;

	while (!WindowShouldClose())
	{
		constexpr float maxScale = 2.0f;
		constexpr float minScale = 0.1f;
		BeginDrawing();
		ClearBackground(RAYWHITE);

		if (IsFileDropped()) {
			const FilePathList droppedFiles = LoadDroppedFiles();
			for (int i = 0; i < droppedFiles.count; i++) {
				std::cout << droppedFiles.paths[i] << std::endl;
				if (stringEndsWith(droppedFiles.paths[i], ".png")) {
					loadAtlas(spritesheet, imageDimensions, doc, frameRects, originalFrameRects, originalFrameData, frameNames, container, droppedFiles.paths[i]);
					selectedFrame = -1;
				}
			}
			UnloadDroppedFiles(droppedFiles);
		}

		bool guiHovered = CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = 980, .y = 0, .width = 300, .height = 720});

		if (editMode && spritesheet.width > 0 && !guiHovered) {
			Vector2 mouseScreen = GetMousePosition();
			Vector2 mouseWorld = GetScreenToWorld2D(mouseScreen, camSpritesheet);
			Vector2 mouseImg = Vector2Scale(mouseWorld, 1.0f / scale);

			if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
				int handle = getContainerHandle(container, mouseImg, HANDLE_SIZE / scale);
				bool hitFrame = false;
				for (int i = static_cast<int>(frameRects.size()) - 1; i >= 0; --i) {
					if (CheckCollisionPointRec(mouseImg, frameRects[i])) {
						selectedFrame = i;
						dragMode = DRAG_FRAME;
						dragStart = mouseImg;
						dragFrameStart = frameRects[i];
						hitFrame = true;
						break;
					}
				}
				if (!hitFrame) {
					if (handle >= 0) {
						dragMode = DRAG_CONTAINER_HANDLE;
						dragHandle = handle;
						dragStart = mouseImg;
						dragContainerStart = container;
						selectedFrame = -1;
					}
					else if (handle == -1) {
						dragMode = DRAG_CONTAINER_BODY;
						dragStart = mouseImg;
						dragContainerStart = container;
						selectedFrame = -1;
					}
					else {
						selectedFrame = -1;
						dragMode = DRAG_NONE;
					}
				}
			}

			if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && dragMode != DRAG_NONE) {
				Vector2 delta = Vector2Subtract(mouseImg, dragStart);

				if (dragMode == DRAG_CONTAINER_BODY) {
					container.x = dragContainerStart.x + delta.x;
					container.y = dragContainerStart.y + delta.y;
				}
				else if (dragMode == DRAG_CONTAINER_HANDLE) {
					Rectangle r = dragContainerStart;
					switch (dragHandle) {
						case 0: r.x += delta.x; r.y += delta.y; r.width -= delta.x; r.height -= delta.y; break;
						case 1: r.y += delta.y; r.width += delta.x; r.height -= delta.y; break;
						case 2: r.x += delta.x; r.width -= delta.x; r.height += delta.y; break;
						case 3: r.width += delta.x; r.height += delta.y; break;
						case 4: r.x += delta.x; r.width -= delta.x; break;
						case 5: r.width += delta.x; break;
						case 6: r.y += delta.y; r.height -= delta.y; break;
						case 7: r.height += delta.y; break;
					}
					if (r.width < 0) { r.x += r.width; r.width = -r.width; }
					if (r.height < 0) { r.y += r.height; r.height = -r.height; }
					if (r.width > 1 && r.height > 1) container = r;
				}
				else if (dragMode == DRAG_FRAME && selectedFrame >= 0) {
					frameRects[selectedFrame].x = dragFrameStart.x + delta.x;
					frameRects[selectedFrame].y = dragFrameStart.y + delta.y;
				}
			}

			if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
				dragMode = DRAG_NONE;
			}
		}

		if (!IsKeyDown(KEY_LEFT_CONTROL)) {
			constexpr float minZoom = 0.1f;
			constexpr float maxZoom = 2.0f;
			camSpritesheet.zoom = Clamp(camSpritesheet.zoom + GetMouseWheelMove() * 0.1f, minZoom, maxZoom);
		}
		else {
			scale = Clamp(scale + GetMouseWheelMove() * 0.1f, minScale, maxScale);
		}

		const float camSpeed = 600.0f / camSpritesheet.zoom;
		if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) camSpritesheet.offset.x -= camSpeed * GetFrameTime();
		if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) camSpritesheet.offset.x += camSpeed * GetFrameTime();
		if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) camSpritesheet.offset.y -= camSpeed * GetFrameTime();
		if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) camSpritesheet.offset.y += camSpeed * GetFrameTime();

		if (spritesheet.width <= 0) {
			const auto message = "Drag an image to begin.";
			auto [x, y] = MeasureTextEx(GetFontDefault(), message, 32, 1);
			DrawText(message, static_cast<int>(static_cast<float>(GetRenderWidth()) - x) / 2, static_cast<int>(static_cast<float>(GetRenderHeight()) - y) / 2, 32, BLACK);
		}
		else {
		BeginMode2D(camSpritesheet);
			DrawRectangleLinesEx(Rectangle{.x = 0, .y = 0, .width = imageDimensions.width, .height = imageDimensions.height}, 4, RED);

			for (size_t i = 0; i < frameRects.size() && i < originalFrameRects.size(); ++i) {
				const auto &orig = originalFrameRects[i];
				const auto &cur = frameRects[i];
				Rectangle srcRect = orig;
				Rectangle dstRect = {.x = cur.x, .y = cur.y, .width = cur.width, .height = cur.height};
				DrawTexturePro(spritesheet, srcRect, dstRect, Vector2Zero(), 0.0f, WHITE);
			}

			for (size_t i = 0; i < frameRects.size(); ++i) {
				const auto &rect = frameRects[i];
				if (static_cast<int>(i) == selectedFrame) {
					DrawRectangleLinesEx(rect, 4, YELLOW);
					if (!frameNames[i].empty()) {
						DrawText(frameNames[i].c_str(), static_cast<int>(rect.x), static_cast<int>(rect.y - 16), 14, DARKGRAY);
					}
				}
				else {
					DrawRectangleLinesEx(rect, 3, ColorAlpha(BLUE, 0.7));
				}
			}

			Rectangle scaledContainer{.x = container.x * scale, .y = container.y * scale,
			                         .width = container.width * scale, .height = container.height * scale};
			DrawRectangleLinesEx(scaledContainer, 5, GREEN);

			if (editMode) {
				const float hs = HANDLE_SIZE;
				const float hsh = hs / 2.0f;
				const float sx = scaledContainer.x, sy = scaledContainer.y;
				const float sw = scaledContainer.width, sh = scaledContainer.height;

				Rectangle handles[8] = {
					{.x = sx - hsh, .y = sy - hsh, .width = hs, .height = hs},
					{.x = sx + sw - hsh, .y = sy - hsh, .width = hs, .height = hs},
					{.x = sx - hsh, .y = sy + sh - hsh, .width = hs, .height = hs},
					{.x = sx + sw - hsh, .y = sy + sh - hsh, .width = hs, .height = hs},
					{.x = sx - hsh, .y = sy + sh / 2 - hsh, .width = hs, .height = hs},
					{.x = sx + sw - hsh, .y = sy + sh / 2 - hsh, .width = hs, .height = hs},
					{.x = sx + sw / 2 - hsh, .y = sy - hsh, .width = hs, .height = hs},
					{.x = sx + sw / 2 - hsh, .y = sy + sh - hsh, .width = hs, .height = hs},
				};
				for (const auto &h : handles) {
					DrawRectangleRec(h, GREEN);
					DrawRectangleLinesEx(h, 2, DARKGREEN);
				}
			}
		EndMode2D();

		}

		DrawText(TextFormat("Scale: %.03lf", scale), 1000, 12, 32, BLACK);
		GuiSlider(Rectangle{.x = 1000, .y = 50, .width = 250, .height = 25}, TextFormat("%0.1lf", minScale), TextFormat("%0.1lf", maxScale), &scale, minScale, maxScale);
		GuiValueBoxFloat(Rectangle{.x = 1260, .y = 50, .width = 60, .height = 25}, NULL, scaleText, &scale, scaleEditMode);
		if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
			if (CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = 1260, .y = 50, .width = 60, .height = 25})) {
				scaleEditMode = !scaleEditMode;
			}
			else {
				scaleEditMode = false;
			}
		}
		if (!scaleEditMode) {
			snprintf(scaleText, sizeof(scaleText), "%.3f", scale);
		}

		DrawText(TextFormat("Camera Zoom: %.01lf (mouse wheel) | WASD/Arrows: pan", camSpritesheet.zoom), 820, 690, 20, BLACK);

		if (GuiButton(Rectangle{.x = 1000, .y = 100, .width = 250, .height = 25}, "SAVE")) {
			saveOutput(spritesheet, doc, "output.png", "output.xml", scale, container, frameRects, originalFrameRects, originalFrameData);
		}

		if (GuiButton(Rectangle{.x = 1000, .y = 135, .width = 250, .height = 25}, "CHANGE PNG")) {
			std::string pngPath = openFileDlg("PNG Files (*.png)\0*.png\0");
			if (!pngPath.empty()) {
				loadAtlas(spritesheet, imageDimensions, doc, frameRects, originalFrameRects, originalFrameData, frameNames, container, pngPath);
				scale = 1.0f;
				selectedFrame = -1;
			}
		}

		if (GuiButton(Rectangle{.x = 1000, .y = 170, .width = 250, .height = 25}, "SAVE AS")) {
			std::string pngPath = saveFileDlg("PNG Files (*.png)\0*.png\0");
			if (!pngPath.empty()) {
				std::string xmlPath = pngPath;
				xmlPath.replace(xmlPath.length() - 3, 3, "xml");
				saveOutput(spritesheet, doc, pngPath, xmlPath, scale, container, frameRects, originalFrameRects, originalFrameData);
			}
		}

		const char *modeText = editMode ? "MODE: EDIT" : "MODE: PREVIEW";
		if (GuiButton(Rectangle{.x = 1000, .y = 220, .width = 250, .height = 25}, modeText)) {
			editMode = !editMode;
			selectedFrame = -1;
			dragMode = DRAG_NONE;
		}

		if (GuiButton(Rectangle{.x = 1000, .y = 260, .width = 250, .height = 25}, "RESET FRAMES")) {
			for (size_t i = 0; i < frameRects.size() && i < originalFrameRects.size(); ++i) {
				frameRects[i] = originalFrameRects[i];
			}
		}

		if (GuiButton(Rectangle{.x = 1000, .y = 295, .width = 250, .height = 25}, "RESET CONTAINER")) {
			container = Rectangle{.x = 0, .y = 0, .width = imageDimensions.width, .height = imageDimensions.height};
		}

		if (editMode) {
			GuiLabel(Rectangle{.x = 1000, .y = 335, .width = 250, .height = 20}, "Click+drag: move frames/container");
			GuiLabel(Rectangle{.x = 1000, .y = 355, .width = 250, .height = 20}, "Drag green handles: crop container");
		}

		if (selectedFrame >= 0 && selectedFrame < static_cast<int>(frameNames.size())) {
			const auto &f = frameRects[selectedFrame];
			const auto &of = originalFrameRects[selectedFrame];
			const float panelY = static_cast<float>(GetRenderHeight()) - 120.0f;

			DrawRectangle(0, static_cast<int>(panelY), 400, 120, ColorAlpha(LIGHTGRAY, 0.85));
			DrawRectangleLines(0, static_cast<int>(panelY), 400, 120, DARKGRAY);

			DrawText(TextFormat("Frame: %s", frameNames[selectedFrame].c_str()), 10, static_cast<int>(panelY + 8), 16, BLACK);
			DrawText(TextFormat("Original: X:%.0f Y:%.0f W:%.0f H:%.0f", of.x, of.y, of.width, of.height), 10, static_cast<int>(panelY + 28), 14, DARKGRAY);

			DrawText("X:", 10, static_cast<int>(panelY + 52), 14, BLACK);
			GuiValueBoxFloat(Rectangle{.x = 30, .y = panelY + 48, .width = 80, .height = 20}, NULL, frameXText, &frameRects[selectedFrame].x, frameXEdit);
			DrawText("Y:", 120, static_cast<int>(panelY + 52), 14, BLACK);
			GuiValueBoxFloat(Rectangle{.x = 140, .y = panelY + 48, .width = 80, .height = 20}, NULL, frameYText, &frameRects[selectedFrame].y, frameYEdit);

			DrawText(TextFormat("W:%.0f  H:%.0f", f.width, f.height), 240, static_cast<int>(panelY + 52), 14, BLACK);
			DrawText(TextFormat("Current: X:%.0f Y:%.0f", f.x, f.y), 10, static_cast<int>(panelY + 78), 14, BLACK);

			if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
				if (CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = 30, .y = panelY + 48, .width = 80, .height = 20})) {
					frameXEdit = !frameXEdit;
					frameYEdit = false;
				}
				else if (CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = 140, .y = panelY + 48, .width = 80, .height = 20})) {
					frameYEdit = !frameYEdit;
					frameXEdit = false;
				}
				else {
					frameXEdit = false;
					frameYEdit = false;
				}
			}
			if (!frameXEdit) {
				snprintf(frameXText, sizeof(frameXText), "%.1f", frameRects[selectedFrame].x);
			}
			if (!frameYEdit) {
				snprintf(frameYText, sizeof(frameYText), "%.1f", frameRects[selectedFrame].y);
			}
		}

		EndDrawing();
	}

	CloseWindow();

	return 0;
}
