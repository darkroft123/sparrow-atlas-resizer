#include <cmath>
#include <cstdio>
#include <iostream>
#include <ctgmath>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

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

struct AnimGroup {
	std::string name;
	std::vector<int> frameIndices;
};

bool loadAtlas(Texture &spritesheet, Rectangle &imageDimensions, pugi::xml_document &doc,
               std::vector<Rectangle> &frameRects, std::vector<Rectangle> &originalFrameRects,
               std::vector<FrameData> &originalFrameData, std::vector<std::string> &frameNames,
               Rectangle &container, const std::string &pngPath, bool &hasXml) {
	UnloadTexture(spritesheet);

	spritesheet = LoadTexture(pngPath.c_str());
	SetTextureFilter(spritesheet, TEXTURE_FILTER_BILINEAR);
	imageDimensions = Rectangle{.x = 0, .y = 0, .width = static_cast<float>(spritesheet.width), .height = static_cast<float>(spritesheet.height)};

	std::string xmlPath = pngPath;
	xmlPath.replace(xmlPath.length() - 3, 3, "xml");
	const pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());

	frameRects.clear();
	originalFrameRects.clear();
	originalFrameData.clear();
	frameNames.clear();

	if (!result) {
		std::cerr << "XML not found or invalid: " << result.description() << std::endl;
		hasXml = false;
		container = Rectangle{.x = 0, .y = 0, .width = imageDimensions.width, .height = imageDimensions.height};
		return true;
	}

	hasXml = true;

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

bool generateXml(pugi::xml_document &doc, const std::string &pngPath,
                 const Image &img,
                 std::vector<Rectangle> &frameRects, std::vector<Rectangle> &originalFrameRects,
                 std::vector<FrameData> &originalFrameData, std::vector<std::string> &frameNames) {
	const int imgW = img.width;
	const int imgH = img.height;
	const Color *pixels = static_cast<const Color *>(img.data);

	auto pixelAt = [&](int x, int y) -> const Color & {
		return pixels[y * imgW + x];
	};

	auto isRowEmpty = [&](int y) -> bool {
		for (int x = 0; x < imgW; ++x) {
			if (pixelAt(x, y).a > 0) return false;
		}
		return true;
	};

	auto isColEmptyInBand = [&](int x, int y0, int y1) -> bool {
		for (int y = y0; y < y1; ++y) {
			if (pixelAt(x, y).a > 0) return false;
		}
		return true;
	};

	std::vector<int> yEdges;
	{
		bool prevEmpty = true;
		for (int y = 0; y <= imgH; ++y) {
			bool curEmpty = (y < imgH) ? isRowEmpty(y) : true;
			if (prevEmpty && !curEmpty) yEdges.push_back(y);
			else if (!prevEmpty && curEmpty) yEdges.push_back(y);
			prevEmpty = curEmpty;
		}
	}

	doc.reset();
	pugi::xml_node atlas = doc.append_child("TextureAtlas");
	atlas.append_attribute("imagePath").set_value(pngPath.c_str());
	atlas.append_attribute("width").set_value(imgW);
	atlas.append_attribute("height").set_value(imgH);

	frameRects.clear();
	originalFrameRects.clear();
	originalFrameData.clear();
	frameNames.clear();

	int frameIdx = 0;
	for (size_t j = 0; j + 1 < yEdges.size(); j += 2) {
		const int bandY0 = yEdges[j];
		const int bandY1 = yEdges[j + 1];

		std::vector<int> xEdges;
		{
			bool prevEmpty = true;
			for (int x = 0; x <= imgW; ++x) {
				bool curEmpty = (x < imgW) ? isColEmptyInBand(x, bandY0, bandY1) : true;
				if (prevEmpty && !curEmpty) xEdges.push_back(x);
				else if (!prevEmpty && curEmpty) xEdges.push_back(x);
				prevEmpty = curEmpty;
			}
		}

		for (size_t i = 0; i + 1 < xEdges.size(); i += 2) {
			const int fx = xEdges[i];
			const int fy = bandY0;
			const int fw = xEdges[i + 1] - fx;
			const int fh = bandY1 - bandY0;
			if (fw <= 0 || fh <= 0) continue;

			char name[64];
			snprintf(name, sizeof(name), "frame_%d", frameIdx);

			pugi::xml_node sub = atlas.append_child("SubTexture");
			sub.append_attribute("name").set_value(name);
			sub.append_attribute("x").set_value(fx);
			sub.append_attribute("y").set_value(fy);
			sub.append_attribute("width").set_value(fw);
			sub.append_attribute("height").set_value(fh);
			sub.append_attribute("frameX").set_value(0);
			sub.append_attribute("frameY").set_value(0);
			sub.append_attribute("frameWidth").set_value(fw);
			sub.append_attribute("frameHeight").set_value(fh);

			Rectangle full{.x = static_cast<float>(fx), .y = static_cast<float>(fy),
			               .width = static_cast<float>(fw), .height = static_cast<float>(fh)};
			frameRects.push_back(full);
			originalFrameRects.push_back(full);
			originalFrameData.push_back(FrameData{0, 0, static_cast<float>(fw), static_cast<float>(fh)});
			frameNames.push_back(name);
			frameIdx++;
		}
	}

	if (frameRects.empty()) {
		pugi::xml_node sub = atlas.append_child("SubTexture");
		sub.append_attribute("name").set_value("full");
		sub.append_attribute("x").set_value(0);
		sub.append_attribute("y").set_value(0);
		sub.append_attribute("width").set_value(imgW);
		sub.append_attribute("height").set_value(imgH);
		sub.append_attribute("frameX").set_value(0);
		sub.append_attribute("frameY").set_value(0);
		sub.append_attribute("frameWidth").set_value(imgW);
		sub.append_attribute("frameHeight").set_value(imgH);

		Rectangle full{.x = 0, .y = 0, .width = static_cast<float>(imgW), .height = static_cast<float>(imgH)};
		frameRects.push_back(full);
		originalFrameRects.push_back(full);
		originalFrameData.push_back(FrameData{0, 0, static_cast<float>(imgW), static_cast<float>(imgH)});
		frameNames.push_back("full");
	}

	std::string xmlPath = pngPath;
	xmlPath.replace(xmlPath.length() - 3, 3, "xml");
	if (!doc.save_file(xmlPath.c_str())) {
		std::cerr << "Failed to save XML to " << xmlPath << std::endl;
		return false;
	}

	return true;
}

void detectAnimGroups(const std::vector<std::string> &frameNames, std::vector<AnimGroup> &groups) {
	groups.clear();
	if (frameNames.empty()) return;

	std::map<std::string, size_t> prefixToIdx;

	for (size_t i = 0; i < frameNames.size(); ++i) {
		const std::string &name = frameNames[i];

		size_t numStart = name.length();
		while (numStart > 0 && std::isdigit(static_cast<unsigned char>(name[numStart - 1]))) {
			numStart--;
		}

		std::string prefix = (numStart > 0) ? name.substr(0, numStart) : name;
		if (prefix.empty()) prefix = name;

		auto it = prefixToIdx.find(prefix);
		if (it == prefixToIdx.end()) {
			prefixToIdx[prefix] = groups.size();
			groups.push_back(AnimGroup{prefix, {static_cast<int>(i)}});
		} else {
			groups[it->second].frameIndices.push_back(static_cast<int>(i));
		}
	}

	for (auto &group : groups) {
		std::sort(group.frameIndices.begin(), group.frameIndices.end(),
			[&frameNames](int a, int b) { return frameNames[a] < frameNames[b]; });
	}

	AnimGroup allGroup{"ALL", {}};
	allGroup.frameIndices.reserve(frameNames.size());
	for (int i = 0; i < static_cast<int>(frameNames.size()); ++i) {
		allGroup.frameIndices.push_back(i);
	}
	groups.insert(groups.begin(), allGroup);
}

void saveOutput(const Texture &spritesheet, const pugi::xml_document &doc, const std::string &pngPath,
                const std::string &xmlPath, float scale, const Rectangle &container,
                const std::vector<Rectangle> &frameRects, const std::vector<Rectangle> &originalFrameRects,
                const std::vector<FrameData> &originalFrameData, const std::vector<std::string> &frameNames) {
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
			frameIt->attribute("frameX").set_value(originalFrameData[i].frameX * scale);
			frameIt->attribute("frameY").set_value(originalFrameData[i].frameY * scale);
			frameIt->attribute("frameWidth").set_value(originalFrameData[i].frameWidth * scale);
			frameIt->attribute("frameHeight").set_value(originalFrameData[i].frameHeight * scale);
		}

		if (i < frameNames.size()) {
			frameIt->attribute("name").set_value(frameNames[i].c_str());
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
	bool hasXml = false;
	std::string currentPngPath;

	DragMode dragMode = DRAG_NONE;
	Vector2 dragStart = {};
	Rectangle dragContainerStart = {};
	Rectangle dragFrameStart = {};
	int dragHandle = -1;

	char frameXText[32] = "";
	char frameYText[32] = "";
	bool frameXEdit = false;
	bool frameYEdit = false;
	int prevSelectedFrame = -1;

	char frameNameText[128] = "";
	bool frameNameEdit = false;

	char frameWText[32] = "";
	char frameHText[32] = "";
	bool frameWEdit = false;
	bool frameHEdit = false;

	char frameFXText[32] = "";
	char frameFYText[32] = "";
	char frameFWText[32] = "";
	char frameFHText[32] = "";
	bool frameFXEdit = false;
	bool frameFYEdit = false;
	bool frameFWEdit = false;
	bool frameFHEdit = false;

	bool animPlaying = false;
	float animTimer = 0.0f;
	int animFrame = 0;
	float animSpeed = 8.0f;
	std::vector<AnimGroup> animGroups;
	int selectedAnim = 0;

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
					currentPngPath = droppedFiles.paths[i];
					loadAtlas(spritesheet, imageDimensions, doc, frameRects, originalFrameRects, originalFrameData, frameNames, container, currentPngPath, hasXml);
					selectedFrame = -1;
					prevSelectedFrame = -1;
					animFrame = 0;
					animPlaying = false;
					if (hasXml) detectAnimGroups(frameNames, animGroups);
					selectedAnim = 0;
				}
			}
			UnloadDroppedFiles(droppedFiles);
		}

		bool guiHovered = CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = 980, .y = 0, .width = 300, .height = 720});
		bool anyTextEdit = frameNameEdit || frameXEdit || frameYEdit || frameWEdit || frameHEdit ||
		                     frameFXEdit || frameFYEdit || frameFWEdit || frameFHEdit || scaleEditMode;

		if (selectedFrame >= 0) {
			guiHovered = guiHovered || CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = 0, .y = static_cast<float>(GetRenderHeight()) - 180.0f, .width = 600, .height = 180});
		}

		if (editMode && spritesheet.width > 0 && !guiHovered) {
			Vector2 mouseScreen = GetMousePosition();
			Vector2 mouseImg = GetScreenToWorld2D(mouseScreen, camSpritesheet);

			if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
				int handle = getContainerHandle(container, mouseImg, HANDLE_SIZE / camSpritesheet.zoom);
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
						prevSelectedFrame = -1;
					}
					else if (handle == -1) {
						dragMode = DRAG_CONTAINER_BODY;
						dragStart = mouseImg;
						dragContainerStart = container;
						selectedFrame = -1;
						prevSelectedFrame = -1;
					}
					else {
						selectedFrame = -1;
						prevSelectedFrame = -1;
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

		if (!anyTextEdit) {
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
		}

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

			DrawRectangleLinesEx(container, 5, GREEN);

			if (editMode) {
				const float hs = HANDLE_SIZE;
				const float hsh = hs / 2.0f;
				const float sx = container.x, sy = container.y;
				const float sw = container.width, sh = container.height;

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

		if (spritesheet.width > 0) {
			const int outW = static_cast<int>(std::round(container.width * scale));
			const int outH = static_cast<int>(std::round(container.height * scale));
			DrawText(TextFormat("Output: %d x %d", outW, outH), 1000, 80, 14, DARKGRAY);
		}

		DrawText(TextFormat("Camera Zoom: %.01lf (mouse wheel) | WASD/Arrows: pan", camSpritesheet.zoom), 820, 690, 20, BLACK);

		if (GuiButton(Rectangle{.x = 1000, .y = 100, .width = 250, .height = 25}, "SAVE")) {
			if (!currentPngPath.empty()) {
				std::string outPng = currentPngPath;
				outPng.replace(outPng.length() - 4, 4, "_out.png");
				std::string outXml = currentPngPath;
				outXml.replace(outXml.length() - 4, 4, "_out.xml");
				saveOutput(spritesheet, doc, outPng, outXml, scale, container, frameRects, originalFrameRects, originalFrameData, frameNames);
			}
		}

		if (GuiButton(Rectangle{.x = 1000, .y = 135, .width = 250, .height = 25}, "CHANGE PNG")) {
			std::string pngPath = openFileDlg("PNG Files (*.png)\0*.png\0");
			if (!pngPath.empty()) {
				currentPngPath = pngPath;
				loadAtlas(spritesheet, imageDimensions, doc, frameRects, originalFrameRects, originalFrameData, frameNames, container, currentPngPath, hasXml);
				scale = 1.0f;
				selectedFrame = -1;
				prevSelectedFrame = -1;
				animFrame = 0;
				animPlaying = false;
				if (hasXml) detectAnimGroups(frameNames, animGroups);
				selectedAnim = 0;
			}
		}

		if (GuiButton(Rectangle{.x = 1000, .y = 170, .width = 250, .height = 25}, "SAVE AS")) {
			std::string pngPath = saveFileDlg("PNG Files (*.png)\0*.png\0");
			if (!pngPath.empty()) {
				std::string xmlPath = pngPath;
				xmlPath.replace(xmlPath.length() - 3, 3, "xml");
				saveOutput(spritesheet, doc, pngPath, xmlPath, scale, container, frameRects, originalFrameRects, originalFrameData, frameNames);
			}
		}

		if (!hasXml && spritesheet.width > 0) {
			if (GuiButton(Rectangle{.x = 1000, .y = 205, .width = 250, .height = 25}, "GENERATE XML")) {
				if (!currentPngPath.empty()) {
					Image srcImg = LoadImageFromTexture(spritesheet);
					generateXml(doc, currentPngPath, srcImg,
					            frameRects, originalFrameRects, originalFrameData, frameNames);
					UnloadImage(srcImg);
					hasXml = true;
					container = Rectangle{.x = 0, .y = 0, .width = imageDimensions.width, .height = imageDimensions.height};
					detectAnimGroups(frameNames, animGroups);
					selectedAnim = 0;
					animFrame = 0;
				}
			}
		}

		const char *modeText = editMode ? "MODE: EDIT" : "MODE: PREVIEW";
		if (GuiButton(Rectangle{.x = 1000, .y = 255, .width = 250, .height = 25}, modeText)) {
			editMode = !editMode;
			selectedFrame = -1;
			prevSelectedFrame = -1;
			dragMode = DRAG_NONE;
		}

		if (GuiButton(Rectangle{.x = 1000, .y = 290, .width = 250, .height = 25}, "RESET FRAMES")) {
			for (size_t i = 0; i < frameRects.size() && i < originalFrameRects.size(); ++i) {
				frameRects[i] = originalFrameRects[i];
			}
		}

		if (GuiButton(Rectangle{.x = 1000, .y = 325, .width = 250, .height = 25}, "RESET CONTAINER")) {
			container = Rectangle{.x = 0, .y = 0, .width = imageDimensions.width, .height = imageDimensions.height};
		}

		if (editMode) {
			GuiLabel(Rectangle{.x = 1000, .y = 365, .width = 250, .height = 20}, "Click+drag: move frames/container");
			GuiLabel(Rectangle{.x = 1000, .y = 385, .width = 250, .height = 20}, "Drag green handles: crop container");
		}

		if (!editMode && spritesheet.width > 0 && !frameRects.empty() && !animGroups.empty()) {
			const float pvX = 1000.0f;
			const float pvY = 365.0f;
			const float pvW = 250.0f;
			const float pvH = 200.0f;

			const auto &groupFrames = animGroups[selectedAnim].frameIndices;
			const int groupCount = static_cast<int>(groupFrames.size());

			DrawRectangle(static_cast<int>(pvX), static_cast<int>(pvY), static_cast<int>(pvW), static_cast<int>(pvH), ColorAlpha(DARKGRAY, 0.9f));
			DrawRectangleLinesEx(Rectangle{pvX, pvY, pvW, pvH}, 2, BLACK);
			DrawText("ANIMATION PREVIEW", static_cast<int>(pvX + 5), static_cast<int>(pvY + 5), 14, WHITE);

			if (animPlaying && groupCount > 0) {
				animTimer += GetFrameTime();
				float interval = 1.0f / animSpeed;
				if (interval > 0 && animTimer >= interval) {
					animTimer -= interval;
					animFrame = (animFrame + 1) % groupCount;
				}
			}

			if (groupCount > 0 && animFrame >= 0 && animFrame < groupCount) {
				const int globalIdx = groupFrames[animFrame];
				const auto &src = frameRects[globalIdx];
				float fitScaleX = (pvW - 20.0f) / src.width;
				float fitScaleY = (pvH - 30.0f) / src.height;
				float fitScale = std::min(fitScaleX, fitScaleY);
				if (fitScale > 1.0f) fitScale = 1.0f;
				float drawW = src.width * fitScale;
				float drawH = src.height * fitScale;
				float drawX = pvX + (pvW - drawW) / 2.0f;
				float drawY = pvY + 20.0f + (pvH - 20.0f - drawH) / 2.0f;

				Rectangle dstRect = {drawX, drawY, drawW, drawH};
				DrawTexturePro(spritesheet, src, dstRect, Vector2Zero(), 0.0f, WHITE);

				if (globalIdx < static_cast<int>(frameNames.size())) {
					DrawText(frameNames[globalIdx].c_str(), static_cast<int>(pvX + 5), static_cast<int>(pvY + pvH - 18), 12, LIGHTGRAY);
				}
			}

			float grpY = pvY + pvH + 5.0f;

			if (GuiButton(Rectangle{pvX, grpY, 30, 22}, "<")) {
				selectedAnim = (selectedAnim - 1 + static_cast<int>(animGroups.size())) % static_cast<int>(animGroups.size());
				animFrame = 0;
				animTimer = 0;
			}
			DrawText(animGroups[selectedAnim].name.c_str(), static_cast<int>(pvX + 35), static_cast<int>(grpY + 4), 14, BLACK);
			DrawText(TextFormat("(%d frames)", groupCount), static_cast<int>(pvX + 35 + MeasureText(animGroups[selectedAnim].name.c_str(), 14) + 10), static_cast<int>(grpY + 4), 12, DARKGRAY);
			if (GuiButton(Rectangle{pvX + pvW - 30, grpY, 30, 22}, ">")) {
				selectedAnim = (selectedAnim + 1) % static_cast<int>(animGroups.size());
				animFrame = 0;
				animTimer = 0;
			}

			float ctrlY = grpY + 28.0f;

			if (GuiButton(Rectangle{pvX, ctrlY, 40, 22}, "|<")) {
				animFrame = (animFrame - 1 + groupCount) % groupCount;
				animPlaying = false;
				animTimer = 0;
			}
			if (GuiButton(Rectangle{pvX + 45, ctrlY, 40, 22}, animPlaying ? "||" : ">")) {
				animPlaying = !animPlaying;
				animTimer = 0;
			}
			if (GuiButton(Rectangle{pvX + 90, ctrlY, 40, 22}, ">|")) {
				animFrame = (animFrame + 1) % groupCount;
				animPlaying = false;
				animTimer = 0;
			}

			DrawText(TextFormat("%d / %d", animFrame + 1, groupCount),
			         static_cast<int>(pvX + 140), static_cast<int>(ctrlY + 3), 14, BLACK);

			float spdY = ctrlY + 28.0f;
			DrawText("FPS:", static_cast<int>(pvX), static_cast<int>(spdY + 3), 14, BLACK);
			GuiSlider(Rectangle{pvX + 35, spdY, 215, 20}, "1", "30", &animSpeed, 1.0f, 30.0f);
		}

		if (selectedFrame >= 0 && selectedFrame < static_cast<int>(frameNames.size())) {
			if (selectedFrame != prevSelectedFrame) {
				frameXEdit = false;
				frameYEdit = false;
				frameWEdit = false;
				frameHEdit = false;
				frameFXEdit = false;
				frameFYEdit = false;
				frameFWEdit = false;
				frameFHEdit = false;
				frameNameEdit = false;
				prevSelectedFrame = selectedFrame;
				snprintf(frameNameText, sizeof(frameNameText), "%s", frameNames[selectedFrame].c_str());
			}

			const auto &f = frameRects[selectedFrame];
			const auto &of = originalFrameRects[selectedFrame];
			const auto &od = originalFrameData[selectedFrame];
			const float panelY = static_cast<float>(GetRenderHeight()) - 180.0f;

			DrawRectangle(0, static_cast<int>(panelY), 600, 180, ColorAlpha(LIGHTGRAY, 0.85));
			DrawRectangleLines(0, static_cast<int>(panelY), 600, 180, DARKGRAY);

			DrawText("Name:", 10, static_cast<int>(panelY + 6), 14, BLACK);
			GuiTextBox(Rectangle{.x = 60, .y = panelY + 2, .width = 200, .height = 20}, frameNameText, sizeof(frameNameText), frameNameEdit);
			if (frameNameEdit) {
				frameNames[selectedFrame] = frameNameText;
			}

			DrawText(TextFormat("Orig: X:%.0f Y:%.0f W:%.0f H:%.0f", of.x, of.y, of.width, of.height), 10, static_cast<int>(panelY + 26), 12, DARKGRAY);

			const float r1 = panelY + 42.0f;
			DrawText("X:", 10, static_cast<int>(r1 + 4), 14, BLACK);
			GuiValueBoxFloat(Rectangle{.x = 28, .y = r1, .width = 65, .height = 20}, NULL, frameXText, &frameRects[selectedFrame].x, frameXEdit);
			DrawText("Y:", 100, static_cast<int>(r1 + 4), 14, BLACK);
			GuiValueBoxFloat(Rectangle{.x = 118, .y = r1, .width = 65, .height = 20}, NULL, frameYText, &frameRects[selectedFrame].y, frameYEdit);
			DrawText("W:", 195, static_cast<int>(r1 + 4), 14, BLACK);
			GuiValueBoxFloat(Rectangle{.x = 213, .y = r1, .width = 65, .height = 20}, NULL, frameWText, &frameRects[selectedFrame].width, frameWEdit);
			DrawText("H:", 290, static_cast<int>(r1 + 4), 14, BLACK);
			GuiValueBoxFloat(Rectangle{.x = 308, .y = r1, .width = 65, .height = 20}, NULL, frameHText, &frameRects[selectedFrame].height, frameHEdit);

			const float r2 = panelY + 66.0f;
			DrawText("fX:", 10, static_cast<int>(r2 + 4), 14, DARKGREEN);
			GuiValueBoxFloat(Rectangle{.x = 32, .y = r2, .width = 65, .height = 20}, NULL, frameFXText, &originalFrameData[selectedFrame].frameX, frameFXEdit);
			DrawText("fY:", 100, static_cast<int>(r2 + 4), 14, DARKGREEN);
			GuiValueBoxFloat(Rectangle{.x = 122, .y = r2, .width = 65, .height = 20}, NULL, frameFYText, &originalFrameData[selectedFrame].frameY, frameFYEdit);
			DrawText("fW:", 195, static_cast<int>(r2 + 4), 14, DARKGREEN);
			GuiValueBoxFloat(Rectangle{.x = 220, .y = r2, .width = 65, .height = 20}, NULL, frameFWText, &originalFrameData[selectedFrame].frameWidth, frameFWEdit);
			DrawText("fH:", 290, static_cast<int>(r2 + 4), 14, DARKGREEN);
			GuiValueBoxFloat(Rectangle{.x = 315, .y = r2, .width = 65, .height = 20}, NULL, frameFHText, &originalFrameData[selectedFrame].frameHeight, frameFHEdit);

			const float r3 = panelY + 90.0f;
			DrawText(TextFormat("Cur: X:%.0f Y:%.0f W:%.0f H:%.0f", f.x, f.y, f.width, f.height), 10, static_cast<int>(r3), 14, BLACK);

			if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
				auto toggleEdit = [&](bool &target, float x, float y, float w, float h) {
					if (CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = x, .y = y, .width = w, .height = h})) {
						target = !target;
						frameNameEdit = false;
						frameXEdit = false;
						frameYEdit = false;
						frameWEdit = false;
						frameHEdit = false;
						frameFXEdit = false;
						frameFYEdit = false;
						frameFWEdit = false;
						frameFHEdit = false;
						target = true;
					}
				};

				bool clickedAny = false;
				auto tryClick = [&](bool &target, float x, float y, float w, float h) {
					if (CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = x, .y = y, .width = w, .height = h})) {
						target = !target;
						clickedAny = true;
					}
				};

				bool newNameState = frameNameEdit;
				bool newXState = frameXEdit, newYState = frameYEdit;
				bool newWState = frameWEdit, newHState = frameHEdit;
				bool newFXState = frameFXEdit, newFYState = frameFYEdit;
				bool newFWState = frameFWEdit, newFHState = frameFHEdit;

				if (CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = 60, .y = panelY + 2, .width = 200, .height = 20})) {
					newNameState = !frameNameEdit; clickedAny = true;
				}
				if (CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = 28, .y = r1, .width = 65, .height = 20})) {
					newXState = !frameXEdit; clickedAny = true;
				}
				if (CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = 118, .y = r1, .width = 65, .height = 20})) {
					newYState = !frameYEdit; clickedAny = true;
				}
				if (CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = 213, .y = r1, .width = 65, .height = 20})) {
					newWState = !frameWEdit; clickedAny = true;
				}
				if (CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = 308, .y = r1, .width = 65, .height = 20})) {
					newHState = !frameHEdit; clickedAny = true;
				}
				if (CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = 32, .y = r2, .width = 65, .height = 20})) {
					newFXState = !frameFXEdit; clickedAny = true;
				}
				if (CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = 122, .y = r2, .width = 65, .height = 20})) {
					newFYState = !frameFYEdit; clickedAny = true;
				}
				if (CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = 220, .y = r2, .width = 65, .height = 20})) {
					newFWState = !frameFWEdit; clickedAny = true;
				}
				if (CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = 315, .y = r2, .width = 65, .height = 20})) {
					newFHState = !frameFHEdit; clickedAny = true;
				}

				if (clickedAny) {
					frameNameEdit = newNameState;
					frameXEdit = newXState;
					frameYEdit = newYState;
					frameWEdit = newWState;
					frameHEdit = newHState;
					frameFXEdit = newFXState;
					frameFYEdit = newFYState;
					frameFWEdit = newFWState;
					frameFHEdit = newFHState;
				}
				else {
					frameNameEdit = false;
					frameXEdit = false;
					frameYEdit = false;
					frameWEdit = false;
					frameHEdit = false;
					frameFXEdit = false;
					frameFYEdit = false;
					frameFWEdit = false;
					frameFHEdit = false;
				}
			}
			if (!frameXEdit) snprintf(frameXText, sizeof(frameXText), "%.1f", frameRects[selectedFrame].x);
			if (!frameYEdit) snprintf(frameYText, sizeof(frameYText), "%.1f", frameRects[selectedFrame].y);
			if (!frameWEdit) snprintf(frameWText, sizeof(frameWText), "%.1f", frameRects[selectedFrame].width);
			if (!frameHEdit) snprintf(frameHText, sizeof(frameHText), "%.1f", frameRects[selectedFrame].height);
			if (!frameFXEdit) snprintf(frameFXText, sizeof(frameFXText), "%.1f", originalFrameData[selectedFrame].frameX);
			if (!frameFYEdit) snprintf(frameFYText, sizeof(frameFYText), "%.1f", originalFrameData[selectedFrame].frameY);
			if (!frameFWEdit) snprintf(frameFWText, sizeof(frameFWText), "%.1f", originalFrameData[selectedFrame].frameWidth);
			if (!frameFHEdit) snprintf(frameFHText, sizeof(frameFHText), "%.1f", originalFrameData[selectedFrame].frameHeight);
		}

		EndDrawing();
	}

	CloseWindow();

	return 0;
}
