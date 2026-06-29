#include <cmath>
#include <cstdio>
#include <iostream>
#include <ctgmath>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cfloat>

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

struct GroupCanvasInfo {
	float frameWidth;
	float frameHeight;
	std::vector<std::pair<float, float>> frameOffsets;
};

GroupCanvasInfo computeGroupCanvas(const std::vector<Rectangle> &frameRects,
                                    const std::vector<FrameData> &originalFrameData,
                                    const std::vector<int> &frameIndices) {
	float minFX = FLT_MAX, minFY = FLT_MAX;
	float maxRight = -FLT_MAX, maxBottom = -FLT_MAX;
	for (int fi : frameIndices) {
		if (fi < 0 || fi >= static_cast<int>(frameRects.size()) || fi >= static_cast<int>(originalFrameData.size())) continue;
		float fx = originalFrameData[fi].frameX;
		float fy = originalFrameData[fi].frameY;
		float rw = frameRects[fi].width;
		float rh = frameRects[fi].height;
		minFX = std::min(minFX, fx);
		minFY = std::min(minFY, fy);
		maxRight = std::max(maxRight, fx + rw);
		maxBottom = std::max(maxBottom, fy + rh);
	}
	GroupCanvasInfo info;
	info.frameWidth = maxRight - minFX;
	info.frameHeight = maxBottom - minFY;
	for (int fi : frameIndices) {
		float fx = (fi < static_cast<int>(originalFrameData.size())) ? originalFrameData[fi].frameX : 0.0f;
		float fy = (fi < static_cast<int>(originalFrameData.size())) ? originalFrameData[fi].frameY : 0.0f;
		info.frameOffsets.push_back({fx - minFX, fy - minFY});
	}
	return info;
}

void saveOutput(const Texture &spritesheet, const pugi::xml_document &doc, const std::string &pngPath,
                const std::string &xmlPath, float scale, const Rectangle &container,
                const std::vector<Rectangle> &frameRects, const std::vector<Rectangle> &originalFrameRects,
                const std::vector<FrameData> &originalFrameData, const std::vector<std::string> &frameNames,
                const std::vector<AnimGroup> &animGroups) {
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
	outAtlas.append_attribute("imagePath").set_value(pngPath.c_str());
	outAtlas.append_attribute("width").set_value(outW);
	outAtlas.append_attribute("height").set_value(outH);

	for (size_t i = 0; i < frameRects.size(); ++i) {
		pugi::xml_node sub = outAtlas.append_child("SubTexture");
		if (i < frameNames.size()) {
			sub.append_attribute("name").set_value(frameNames[i].c_str());
		}
		const float newX = (frameRects[i].x - container.x) * scale;
		const float newY = (frameRects[i].y - container.y) * scale;
		const float newW = frameRects[i].width * scale;
		const float newH = frameRects[i].height * scale;
		sub.append_attribute("x").set_value(newX);
		sub.append_attribute("y").set_value(newY);
		sub.append_attribute("width").set_value(newW);
		sub.append_attribute("height").set_value(newH);

		bool foundGroup = false;
		for (size_t g = 1; g < animGroups.size(); ++g) {
			const auto &grp = animGroups[g];
			for (size_t k = 0; k < grp.frameIndices.size(); ++k) {
				if (grp.frameIndices[k] == static_cast<int>(i)) {
					GroupCanvasInfo ci = computeGroupCanvas(frameRects, originalFrameData, grp.frameIndices);
					sub.append_attribute("frameX").set_value(ci.frameOffsets[k].first * scale);
					sub.append_attribute("frameY").set_value(ci.frameOffsets[k].second * scale);
					sub.append_attribute("frameWidth").set_value(ci.frameWidth * scale);
					sub.append_attribute("frameHeight").set_value(ci.frameHeight * scale);
					foundGroup = true;
					break;
				}
			}
			if (foundGroup) break;
		}
		if (!foundGroup && i < originalFrameData.size()) {
			sub.append_attribute("frameX").set_value(originalFrameData[i].frameX * scale);
			sub.append_attribute("frameY").set_value(originalFrameData[i].frameY * scale);
			sub.append_attribute("frameWidth").set_value(originalFrameData[i].frameWidth * scale);
			sub.append_attribute("frameHeight").set_value(originalFrameData[i].frameHeight * scale);
		}
	}

	outDoc.save_file(xmlPath.c_str());
}

enum DragMode { DRAG_NONE, DRAG_CONTAINER_BODY, DRAG_CONTAINER_HANDLE, DRAG_FRAME };
enum AppState { STATE_MAIN, STATE_ANIM_EDITOR };

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

	AppState appState = STATE_MAIN;
	float editorScrollY = 0.0f;
	int editorSelectedFrame = -1;
	int editorSelectedAnim = 0;
	bool editorDragging = false;
	Vector2 editorDragStart = {};
	bool editorZoomed = false;
	float editorPreviewZoom = 1.0f;
	bool editorShowFrameBox = false;

	bool darkMode = false;

	struct Theme {
		Color bg;
		Color panelBg;
		Color panelBgAlt;
		Color headerBg;
		Color text;
		Color textDim;
		Color textBright;
		Color border;
		Color inputBg;
		Color inputBorder;
		Color previewBg;
		Color accent;
		Color accentText;
		Color selectedBg;
	};

	auto applyGuiTheme = [&]() {
		if (darkMode) {
			GuiSetStyle(DEFAULT, BACKGROUND_COLOR, 0x1e1e1eff);
			GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, 0x505050ff);
			GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, 0x373737ff);
			GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, 0xdcdcdcff);
			GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, 0x64a0ffff);
			GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, 0x464659ff);
			GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, 0xdcdcdcff);
			GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, 0x64a0ffff);
			GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED, 0x64a0ffff);
			GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED, 0xffffffff);
			GuiSetStyle(DEFAULT, BORDER_COLOR_DISABLED, 0x505050ff);
			GuiSetStyle(DEFAULT, BASE_COLOR_DISABLED, 0x323232ff);
			GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED, 0x8c8c8cff);
			GuiSetStyle(DEFAULT, LINE_COLOR, 0x505050ff);
			GuiSetStyle(SLIDER, BASE_COLOR_NORMAL, 0x505050ff);
		} else {
			GuiSetStyle(DEFAULT, BACKGROUND_COLOR, 0xf5f5f5ff);
			GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, 0x838383ff);
			GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, 0xc9c9c9ff);
			GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, 0x686868ff);
			GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, 0x5bb2d9ff);
			GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, 0xc9effeff);
			GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, 0x6c9bbcff);
			GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, 0x0492c7ff);
			GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED, 0x97e8ffff);
			GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED, 0x368bafff);
			GuiSetStyle(DEFAULT, BORDER_COLOR_DISABLED, 0xb5c1c2ff);
			GuiSetStyle(DEFAULT, BASE_COLOR_DISABLED, 0xe6e9e9ff);
			GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED, 0xaeb7b8ff);
			GuiSetStyle(DEFAULT, LINE_COLOR, 0x90abb5ff);
			GuiSetStyle(SLIDER, BASE_COLOR_NORMAL, 0xc9c9c9ff);
		}
	};

	auto getTheme = [&]() -> Theme {
		if (darkMode) {
			return Theme{
				{30, 30, 30, 255},
				{40, 40, 40, 255},
				{50, 50, 50, 255},
				{25, 25, 35, 230},
				{220, 220, 220, 255},
				{140, 140, 140, 255},
				{255, 255, 255, 255},
				{80, 80, 80, 255},
				{55, 55, 55, 255},
				{90, 90, 90, 255},
				{35, 35, 40, 255},
				{100, 160, 255, 255},
				{255, 255, 255, 255},
				{70, 70, 90, 200},
			};
		} else {
			return Theme{
				{230, 230, 230, 255},
				{245, 245, 245, 255},
				{255, 255, 255, 255},
				{220, 220, 230, 230},
				{30, 30, 30, 255},
				{100, 100, 100, 255},
				{0, 0, 0, 255},
				{180, 180, 180, 255},
				{255, 255, 255, 255},
				{160, 160, 160, 255},
				{200, 200, 200, 255},
				{50, 100, 200, 255},
				{255, 255, 255, 255},
				{200, 210, 240, 200},
			};
		}
	};

	applyGuiTheme();

	while (!WindowShouldClose())
	{
		constexpr float maxScale = 2.0f;
		constexpr float minScale = 0.1f;
		BeginDrawing();
		Theme th = getTheme();
		ClearBackground(th.bg);

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

		if (appState == STATE_MAIN) {

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
			DrawText(message, static_cast<int>(static_cast<float>(GetRenderWidth()) - x) / 2, static_cast<int>(static_cast<float>(GetRenderHeight()) - y) / 2, 32, th.text);
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
				DrawRectangleLinesEx(rect, 4, th.accent);
				if (!frameNames[i].empty()) {
					DrawText(frameNames[i].c_str(), static_cast<int>(rect.x), static_cast<int>(rect.y - 16), 14, th.textDim);
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
				DrawRectangleLinesEx(h, 2, th.accent);
				}
			}
		EndMode2D();

		}

		DrawText(TextFormat("Scale: %.03lf", scale), 1000, 12, 32, th.text);
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
			DrawText(TextFormat("Output: %d x %d", outW, outH), 1000, 80, 14, th.textDim);
		}

		DrawText(TextFormat("Camera Zoom: %.01lf (mouse wheel) | WASD/Arrows: pan", camSpritesheet.zoom), 300, 690, 20, th.text);

		if (GuiButton(Rectangle{.x = 1000, .y = 100, .width = 250, .height = 25}, "SAVE")) {
			if (!currentPngPath.empty()) {
				std::string outPng = currentPngPath;
				outPng.replace(outPng.length() - 4, 4, "_out.png");
				std::string outXml = currentPngPath;
				outXml.replace(outXml.length() - 4, 4, "_out.xml");
				saveOutput(spritesheet, doc, outPng, outXml, scale, container, frameRects, originalFrameRects, originalFrameData, frameNames, animGroups);
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
				saveOutput(spritesheet, doc, pngPath, xmlPath, scale, container, frameRects, originalFrameRects, originalFrameData, frameNames, animGroups);
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

		if (!frameRects.empty() && hasXml) {
			if (GuiButton(Rectangle{.x = 1000, .y = 360, .width = 250, .height = 25}, "ANIM EDITOR")) {
				appState = STATE_ANIM_EDITOR;
				if (!animGroups.empty()) {
					detectAnimGroups(frameNames, animGroups);
				}
				editorSelectedFrame = frameRects.empty() ? -1 : 0;
				editorScrollY = 0;
			}
		}

		const char *themeLabel = darkMode ? "LIGHT MODE" : "DARK MODE";
		if (GuiButton(Rectangle{.x = 1000, .y = 680, .width = 250, .height = 25}, themeLabel)) {
			darkMode = !darkMode;
			applyGuiTheme();
		}

		if (editMode) {
			GuiLabel(Rectangle{.x = 1000, .y = 395, .width = 250, .height = 20}, "Click+drag: move frames/container");
			GuiLabel(Rectangle{.x = 1000, .y = 415, .width = 250, .height = 20}, "Drag green handles: crop container");
		}

		if (!editMode && spritesheet.width > 0 && !frameRects.empty() && !animGroups.empty()) {
			const float pvX = 1000.0f;
			const float pvY = 395.0f;
			const float pvW = 250.0f;
			const float pvH = 200.0f;

			const auto &groupFrames = animGroups[selectedAnim].frameIndices;
			const int groupCount = static_cast<int>(groupFrames.size());

			DrawRectangle(static_cast<int>(pvX), static_cast<int>(pvY), static_cast<int>(pvW), static_cast<int>(pvH), th.previewBg);
			DrawRectangleLinesEx(Rectangle{pvX, pvY, pvW, pvH}, 2, th.border);
			DrawText("ANIMATION PREVIEW", static_cast<int>(pvX + 5), static_cast<int>(pvY + 5), 14, th.textBright);

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
					DrawText(frameNames[globalIdx].c_str(), static_cast<int>(pvX + 5), static_cast<int>(pvY + pvH - 18), 12, th.textDim);
				}
			}

			float grpY = pvY + pvH + 5.0f;

			if (GuiButton(Rectangle{pvX, grpY, 30, 22}, "<")) {
				selectedAnim = (selectedAnim - 1 + static_cast<int>(animGroups.size())) % static_cast<int>(animGroups.size());
				animFrame = 0;
				animTimer = 0;
			}
			DrawText(animGroups[selectedAnim].name.c_str(), static_cast<int>(pvX + 35), static_cast<int>(grpY + 4), 14, th.text);
			DrawText(TextFormat("(%d frames)", groupCount), static_cast<int>(pvX + 35 + MeasureText(animGroups[selectedAnim].name.c_str(), 14) + 10), static_cast<int>(grpY + 4), 12, th.textDim);
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
		         static_cast<int>(pvX + 140), static_cast<int>(ctrlY + 3), 14, th.text);

		float spdY = ctrlY + 28.0f;
		DrawText("FPS:", static_cast<int>(pvX), static_cast<int>(spdY + 3), 14, th.text);
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

		DrawRectangle(0, static_cast<int>(panelY), 600, 180, th.panelBg);
		DrawRectangleLines(0, static_cast<int>(panelY), 600, 180, th.border);

		DrawText("Name:", 10, static_cast<int>(panelY + 6), 14, th.text);
			GuiTextBox(Rectangle{.x = 60, .y = panelY + 2, .width = 200, .height = 20}, frameNameText, sizeof(frameNameText), frameNameEdit);
			if (frameNameEdit) {
				frameNames[selectedFrame] = frameNameText;
			}

		DrawText(TextFormat("Orig: X:%.0f Y:%.0f W:%.0f H:%.0f", of.x, of.y, of.width, of.height), 10, static_cast<int>(panelY + 26), 12, th.textDim);

		const float r1 = panelY + 42.0f;
		DrawText("X:", 10, static_cast<int>(r1 + 4), 14, th.text);
		GuiValueBoxFloat(Rectangle{.x = 28, .y = r1, .width = 65, .height = 20}, NULL, frameXText, &frameRects[selectedFrame].x, frameXEdit);
		DrawText("Y:", 100, static_cast<int>(r1 + 4), 14, th.text);
		GuiValueBoxFloat(Rectangle{.x = 118, .y = r1, .width = 65, .height = 20}, NULL, frameYText, &frameRects[selectedFrame].y, frameYEdit);
		DrawText("W:", 195, static_cast<int>(r1 + 4), 14, th.text);
		GuiValueBoxFloat(Rectangle{.x = 213, .y = r1, .width = 65, .height = 20}, NULL, frameWText, &frameRects[selectedFrame].width, frameWEdit);
		DrawText("H:", 290, static_cast<int>(r1 + 4), 14, th.text);
		GuiValueBoxFloat(Rectangle{.x = 308, .y = r1, .width = 65, .height = 20}, NULL, frameHText, &frameRects[selectedFrame].height, frameHEdit);

		const float r2 = panelY + 66.0f;
		DrawText("fX:", 10, static_cast<int>(r2 + 4), 14, th.accent);
		GuiValueBoxFloat(Rectangle{.x = 32, .y = r2, .width = 65, .height = 20}, NULL, frameFXText, &originalFrameData[selectedFrame].frameX, frameFXEdit);
		DrawText("fY:", 100, static_cast<int>(r2 + 4), 14, th.accent);
		GuiValueBoxFloat(Rectangle{.x = 122, .y = r2, .width = 65, .height = 20}, NULL, frameFYText, &originalFrameData[selectedFrame].frameY, frameFYEdit);
		DrawText("fW:", 195, static_cast<int>(r2 + 4), 14, th.accent);
		GuiValueBoxFloat(Rectangle{.x = 220, .y = r2, .width = 65, .height = 20}, NULL, frameFWText, &originalFrameData[selectedFrame].frameWidth, frameFWEdit);
		DrawText("fH:", 290, static_cast<int>(r2 + 4), 14, th.accent);
		GuiValueBoxFloat(Rectangle{.x = 315, .y = r2, .width = 65, .height = 20}, NULL, frameFHText, &originalFrameData[selectedFrame].frameHeight, frameFHEdit);

		const float r3 = panelY + 90.0f;
		DrawText(TextFormat("Cur: X:%.0f Y:%.0f W:%.0f H:%.0f", f.x, f.y, f.width, f.height), 10, static_cast<int>(r3), 14, th.text);

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

		} else if (appState == STATE_ANIM_EDITOR) {

			DrawRectangle(0, 0, 1280, 720, th.bg);

			DrawRectangle(0, 0, 1280, 40, th.headerBg);
			if (GuiButton(Rectangle{10, 8, 100, 25}, "< BACK")) {
				appState = STATE_MAIN;
			}
			DrawText("ANIMATION EDITOR", 400, 12, 18, th.textBright);
			const char *zoomLabel = editorZoomed ? "NORMAL" : "ZOOM";
			if (GuiButton(Rectangle{600, 8, 80, 25}, zoomLabel)) {
				editorZoomed = !editorZoomed;
			}
			if (GuiButton(Rectangle{690, 8, 110, 25}, "CHANGE PNG")) {
				std::string pngPath = openFileDlg("PNG Files (*.png)\0*.png\0");
				if (!pngPath.empty()) {
					currentPngPath = pngPath;
					loadAtlas(spritesheet, imageDimensions, doc, frameRects, originalFrameRects, originalFrameData, frameNames, container, currentPngPath, hasXml);
					scale = 1.0f;
					editorSelectedFrame = frameRects.empty() ? -1 : 0;
					editorScrollY = 0;
					if (hasXml) detectAnimGroups(frameNames, animGroups);
					animFrame = 0;
					animPlaying = false;
				}
			}
			const char *themeLabelEd = darkMode ? "LIGHT" : "DARK";
			if (GuiButton(Rectangle{810, 8, 60, 25}, themeLabelEd)) {
				darkMode = !darkMode;
				applyGuiTheme();
			}
			const char *frameBoxLabel = editorShowFrameBox ? "HIDE BOX" : "SHOW BOX";
			if (GuiButton(Rectangle{880, 8, 80, 25}, frameBoxLabel)) {
				editorShowFrameBox = !editorShowFrameBox;
			}
			if (GuiButton(Rectangle{1150, 8, 120, 25}, "SAVE XML")) {
				if (!currentPngPath.empty()) {
					std::string xmlPath = currentPngPath;
					xmlPath.replace(xmlPath.length() - 3, 3, "xml");
					pugi::xml_document outDoc;
					pugi::xml_node outAtlas = outDoc.append_child("TextureAtlas");
					outAtlas.append_attribute("imagePath").set_value(currentPngPath.c_str());
					outAtlas.append_attribute("width").set_value(static_cast<int>(imageDimensions.width));
					outAtlas.append_attribute("height").set_value(static_cast<int>(imageDimensions.height));

					for (size_t i = 0; i < frameRects.size(); ++i) {
						pugi::xml_node sub = outAtlas.append_child("SubTexture");
						sub.append_attribute("name").set_value(frameNames[i].c_str());
						sub.append_attribute("x").set_value(frameRects[i].x);
						sub.append_attribute("y").set_value(frameRects[i].y);
						sub.append_attribute("width").set_value(frameRects[i].width);
						sub.append_attribute("height").set_value(frameRects[i].height);

						bool foundGroup = false;
						for (size_t g = 1; g < animGroups.size(); ++g) {
							const auto &grp = animGroups[g];
							for (size_t k = 0; k < grp.frameIndices.size(); ++k) {
								if (grp.frameIndices[k] == static_cast<int>(i)) {
									GroupCanvasInfo ci = computeGroupCanvas(frameRects, originalFrameData, grp.frameIndices);
									sub.append_attribute("frameX").set_value(ci.frameOffsets[k].first);
									sub.append_attribute("frameY").set_value(ci.frameOffsets[k].second);
									sub.append_attribute("frameWidth").set_value(ci.frameWidth);
									sub.append_attribute("frameHeight").set_value(ci.frameHeight);
									foundGroup = true;
									break;
								}
							}
							if (foundGroup) break;
						}
						if (!foundGroup && i < originalFrameData.size()) {
							sub.append_attribute("frameX").set_value(originalFrameData[i].frameX);
							sub.append_attribute("frameY").set_value(originalFrameData[i].frameY);
							sub.append_attribute("frameWidth").set_value(originalFrameData[i].frameWidth);
							sub.append_attribute("frameHeight").set_value(originalFrameData[i].frameHeight);
						}
					}
					outDoc.save_file(xmlPath.c_str());
					doc.reset();
					doc.load_file(xmlPath.c_str());
					hasXml = true;
				}
			}

			const float leftW = editorZoomed ? 350.0f : 920.0f;
			const float rightW = editorZoomed ? 910.0f : 340.0f;
			const float topY = 45.0f;
			const float botY = 670.0f;
			const float midH = botY - topY;

			DrawRectangle(0, static_cast<int>(topY), static_cast<int>(leftW), static_cast<int>(midH), th.panelBg);
			DrawRectangle(static_cast<int>(leftW), static_cast<int>(topY), static_cast<int>(rightW), static_cast<int>(midH), th.panelBgAlt);
			DrawLine(static_cast<int>(leftW), static_cast<int>(topY), static_cast<int>(leftW), static_cast<int>(botY), th.border);

			if (IsKeyDown(KEY_LEFT_CONTROL)) {
				editorScrollY -= GetMouseWheelMove() * 40.0f;
			}

			float curY = topY + 5.0f - editorScrollY;
			for (size_t g = 0; g < animGroups.size() && curY < botY; ++g) {
				const auto &grp = animGroups[g];
				const int grpCount = static_cast<int>(grp.frameIndices.size());

				if (curY + 25.0f > topY && curY < botY) {
					bool exp = (static_cast<int>(g) == editorSelectedAnim);
					const char *arrow = exp ? "v" : ">";
					DrawText(TextFormat("%s %s (%d frames)", arrow, grp.name.c_str(), grpCount), static_cast<int>(leftW - leftW + 10), static_cast<int>(curY), 16, th.text);
					if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), Rectangle{0, curY, leftW, 22})) {
						editorSelectedAnim = static_cast<int>(g);
						animPlaying = false;
						animFrame = 0;
					}
					curY += 25.0f;
				}

				if (static_cast<int>(g) == editorSelectedAnim) {
					float thumbX = 10.0f;
					const float thumbSize = 60.0f;
					const float thumbPad = 6.0f;
					for (int f = 0; f < grpCount && curY < botY + 100.0f; ++f) {
						const int fi = grp.frameIndices[f];
						if (curY + thumbSize > topY && curY < botY && thumbX + thumbSize < leftW) {
							const auto &src = frameRects[fi];
							Rectangle thumbRect = {thumbX, curY, thumbSize, thumbSize};
							bool isSelected = (fi == editorSelectedFrame);
						DrawRectangleRec(thumbRect, isSelected ? th.selectedBg : th.panelBgAlt);
						DrawRectangleLinesEx(thumbRect, isSelected ? 3 : 1, isSelected ? th.accent : th.border);

							float fitS = std::min(thumbSize / src.width, thumbSize / src.height);
							if (fitS > 1.0f) fitS = 1.0f;
							float dW = src.width * fitS;
							float dH = src.height * fitS;
							Rectangle dst = {thumbX + (thumbSize - dW) / 2, curY + (thumbSize - dH) / 2, dW, dH};
							DrawTexturePro(spritesheet, src, dst, Vector2Zero(), 0.0f, WHITE);

							DrawText(TextFormat("%d", f), static_cast<int>(thumbX + 2), static_cast<int>(curY + 2), 10, th.textDim);

							if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), thumbRect)) {
								editorSelectedFrame = fi;
								animFrame = f;
							}

							thumbX += thumbSize + thumbPad;
						} else {
							thumbX += thumbSize + thumbPad;
						}
						if (thumbX + thumbSize > leftW) {
							thumbX = 10.0f;
							curY += thumbSize + thumbPad;
						}
					}
					curY += thumbSize + thumbPad + 10.0f;
				}
			}
			if (editorScrollY < 0) editorScrollY = 0;

			float rpX = leftW + 10.0f;
			float rpY = topY + 5.0f;

			int showFrame = editorSelectedFrame;
			if (!animGroups.empty() && editorSelectedAnim >= 0 && editorSelectedAnim < static_cast<int>(animGroups.size())) {
				const auto &grp = animGroups[editorSelectedAnim];
				if (animFrame >= 0 && animFrame < static_cast<int>(grp.frameIndices.size())) {
					showFrame = grp.frameIndices[animFrame];
				}
			}
			if (showFrame < 0 || showFrame >= static_cast<int>(frameRects.size())) {
				showFrame = editorSelectedFrame;
			}

			if (showFrame >= 0 && showFrame < static_cast<int>(frameNames.size())) {
				const auto &f = frameRects[showFrame];

				float prevW = rightW - 20.0f;
				float prevH = editorZoomed ? 350.0f : 120.0f;
				Rectangle prevBg = {rpX, rpY, prevW, prevH};
				DrawRectangle(static_cast<int>(rpX), static_cast<int>(rpY), static_cast<int>(prevW), static_cast<int>(prevH), th.previewBg);

				if (!animGroups.empty() && editorSelectedAnim >= 0 && editorSelectedAnim < static_cast<int>(animGroups.size())) {
					const auto &grp = animGroups[editorSelectedAnim];
					if (!grp.frameIndices.empty()) {
						const int ghostIdx = grp.frameIndices[0];
						if (ghostIdx != showFrame && ghostIdx >= 0 && ghostIdx < static_cast<int>(frameRects.size())) {
							const auto &ghost = frameRects[ghostIdx];
							float gs = std::min(prevW / ghost.width, prevH / ghost.height) * editorPreviewZoom;
							float gw = ghost.width * gs;
							float gh = ghost.height * gs;
							Rectangle gd = {rpX + (prevW - gw) / 2, rpY + (prevH - gh) / 2, gw, gh};
							DrawTexturePro(spritesheet, ghost, gd, Vector2Zero(), 0.0f, ColorAlpha(RED, 0.25f));
						}
					}
				}

				float fitS = std::min(prevW / f.width, prevH / f.height) * editorPreviewZoom;
				float dW = f.width * fitS;
				float dH = f.height * fitS;
				Rectangle prevDst = {rpX + (prevW - dW) / 2, rpY + (prevH - dH) / 2, dW, dH};
				DrawTexturePro(spritesheet, f, prevDst, Vector2Zero(), 0.0f, WHITE);
				DrawRectangleLinesEx(prevBg, 1, th.border);
				if (editorShowFrameBox) {
					DrawRectangleLinesEx(prevDst, 2, th.accent);
				}
				DrawText(frameNames[showFrame].c_str(), static_cast<int>(rpX + 5), static_cast<int>(rpY + prevH + 3), 12, th.text);
				DrawText(TextFormat("Y/E zoom: %.0f%%", editorPreviewZoom * 100), static_cast<int>(rpX + prevW - 110), static_cast<int>(rpY + prevH + 3), 11, th.textDim);
				rpY += prevH + 20.0f;

				if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), prevBg)) {
					editorDragging = true;
					editorDragStart = GetMousePosition();
				}

				if (editorSelectedFrame != showFrame) {
					editorSelectedFrame = showFrame;
					snprintf(frameNameText, sizeof(frameNameText), "%s", frameNames[showFrame].c_str());
				}

				float lblX = rpX;
				float inX = rpX + 35.0f;
				float inW = std::min(120.0f, (rightW - 90.0f) / 2.0f);
				float rowH = 30.0f;

				float origRpY = rpY;

				DrawText("Name:", static_cast<int>(lblX), static_cast<int>(rpY + 4), 14, th.text);
				GuiTextBox(Rectangle{inX, rpY, 200, 26}, frameNameText, sizeof(frameNameText), frameNameEdit);
				if (frameNameEdit) frameNames[showFrame] = frameNameText;
				rpY += rowH;

			DrawText("X:", static_cast<int>(lblX), static_cast<int>(rpY + 4), 14, th.text);
			GuiValueBoxFloat(Rectangle{inX, rpY, inW, 26}, NULL, frameXText, &frameRects[showFrame].x, frameXEdit);
			DrawText("Y:", static_cast<int>(inX + inW + 10), static_cast<int>(rpY + 4), 14, th.text);
				GuiValueBoxFloat(Rectangle{inX + inW + 25, rpY, inW, 26}, NULL, frameYText, &frameRects[showFrame].y, frameYEdit);
				rpY += rowH;

			DrawText("W:", static_cast<int>(lblX), static_cast<int>(rpY + 4), 14, th.text);
			GuiValueBoxFloat(Rectangle{inX, rpY, inW, 26}, NULL, frameWText, &frameRects[showFrame].width, frameWEdit);
			DrawText("H:", static_cast<int>(inX + inW + 10), static_cast<int>(rpY + 4), 14, th.text);
				GuiValueBoxFloat(Rectangle{inX + inW + 25, rpY, inW, 26}, NULL, frameHText, &frameRects[showFrame].height, frameHEdit);
				rpY += rowH;

			DrawText("fX:", static_cast<int>(lblX), static_cast<int>(rpY + 4), 14, th.accent);
			GuiValueBoxFloat(Rectangle{inX, rpY, inW, 26}, NULL, frameFXText, &originalFrameData[showFrame].frameX, frameFXEdit);
			DrawText("fY:", static_cast<int>(inX + inW + 10), static_cast<int>(rpY + 4), 14, th.accent);
				GuiValueBoxFloat(Rectangle{inX + inW + 25, rpY, inW, 26}, NULL, frameFYText, &originalFrameData[showFrame].frameY, frameFYEdit);
				rpY += rowH;

			DrawText("fW:", static_cast<int>(lblX), static_cast<int>(rpY + 4), 14, th.accent);
			GuiValueBoxFloat(Rectangle{inX, rpY, inW, 26}, NULL, frameFWText, &originalFrameData[showFrame].frameWidth, frameFWEdit);
			DrawText("fH:", static_cast<int>(inX + inW + 10), static_cast<int>(rpY + 4), 14, th.accent);
				GuiValueBoxFloat(Rectangle{inX + inW + 25, rpY, inW, 26}, NULL, frameFHText, &originalFrameData[showFrame].frameHeight, frameFHEdit);
				rpY += rowH;

				DrawText(TextFormat("Cur: X:%.0f Y:%.0f W:%.0f H:%.0f", f.x, f.y, f.width, f.height), static_cast<int>(lblX), static_cast<int>(rpY + 2), 11, th.text);
				rpY += rowH;

				if (!frameXEdit) snprintf(frameXText, sizeof(frameXText), "%.1f", frameRects[showFrame].x);
				if (!frameYEdit) snprintf(frameYText, sizeof(frameYText), "%.1f", frameRects[showFrame].y);
				if (!frameWEdit) snprintf(frameWText, sizeof(frameWText), "%.1f", frameRects[showFrame].width);
				if (!frameHEdit) snprintf(frameHText, sizeof(frameHText), "%.1f", frameRects[showFrame].height);
				if (!frameFXEdit) snprintf(frameFXText, sizeof(frameFXText), "%.1f", originalFrameData[showFrame].frameX);
				if (!frameFYEdit) snprintf(frameFYText, sizeof(frameFYText), "%.1f", originalFrameData[showFrame].frameY);
				if (!frameFWEdit) snprintf(frameFWText, sizeof(frameFWText), "%.1f", originalFrameData[showFrame].frameWidth);
				if (!frameFHEdit) snprintf(frameFHText, sizeof(frameFHText), "%.1f", originalFrameData[showFrame].frameHeight);

				if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
					int clickedField = -1;
					auto hitTest = [&](float bx, float by, float bw, float bh) {
						if (clickedField < 0 && CheckCollisionPointRec(GetMousePosition(), Rectangle{bx, by, bw, bh})) {
							clickedField = 0;
						}
					};

					hitTest(inX, origRpY, 200, 26);
					if (clickedField == 0) clickedField = 1;
					else {
						hitTest(inX, origRpY + rowH, inW, 26);
						if (clickedField == 0) clickedField = 2;
						else {
							hitTest(inX + inW + 25, origRpY + rowH, inW, 26);
							if (clickedField == 0) clickedField = 3;
							else {
								hitTest(inX, origRpY + rowH * 2, inW, 26);
								if (clickedField == 0) clickedField = 4;
								else {
									hitTest(inX + inW + 25, origRpY + rowH * 2, inW, 26);
									if (clickedField == 0) clickedField = 5;
									else {
										hitTest(inX, origRpY + rowH * 3, inW, 26);
										if (clickedField == 0) clickedField = 6;
										else {
											hitTest(inX + inW + 25, origRpY + rowH * 3, inW, 26);
											if (clickedField == 0) clickedField = 7;
											else {
												hitTest(inX, origRpY + rowH * 4, inW, 26);
												if (clickedField == 0) clickedField = 8;
												else {
													hitTest(inX + inW + 25, origRpY + rowH * 4, inW, 26);
													if (clickedField == 0) clickedField = 9;
												}
											}
										}
									}
								}
							}
						}
					}

					if (clickedField > 0) {
						frameNameEdit = (clickedField == 1);
						frameXEdit = (clickedField == 2);
						frameYEdit = (clickedField == 3);
						frameWEdit = (clickedField == 4);
						frameHEdit = (clickedField == 5);
						frameFXEdit = (clickedField == 6);
						frameFYEdit = (clickedField == 7);
						frameFWEdit = (clickedField == 8);
						frameFHEdit = (clickedField == 9);
					} else {
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
			} else {
				DrawText("Select a frame to edit", static_cast<int>(rpX), static_cast<int>(rpY + 50), 16, th.textDim);
			}

			const float botBarY = 670.0f;
			if (!animGroups.empty() && editorSelectedAnim >= 0 && editorSelectedAnim < static_cast<int>(animGroups.size())) {
				const auto &grp = animGroups[editorSelectedAnim];
				const int gc = static_cast<int>(grp.frameIndices.size());

				if (animPlaying && gc > 0) {
					animTimer += GetFrameTime();
					float interval = 1.0f / animSpeed;
					if (interval > 0 && animTimer >= interval) {
						animTimer -= interval;
						animFrame = (animFrame + 1) % gc;
					}
				}

				if (GuiButton(Rectangle{10, botBarY, 50, 26}, "|<")) {
					animFrame = (animFrame - 1 + gc) % gc; animPlaying = false; animTimer = 0;
				}
				if (GuiButton(Rectangle{65, botBarY, 50, 26}, animPlaying ? "||" : ">")) {
					animPlaying = !animPlaying; animTimer = 0;
				}
				if (GuiButton(Rectangle{120, botBarY, 50, 26}, ">|")) {
					animFrame = (animFrame + 1) % gc; animPlaying = false; animTimer = 0;
				}
			DrawText(TextFormat("[%s] %d / %d", grp.name.c_str(), animFrame + 1, gc), 180, static_cast<int>(botBarY + 5), 16, th.text);

			DrawText("FPS:", 430, static_cast<int>(botBarY + 5), 14, th.text);
			GuiSlider(Rectangle{470, botBarY, 240, 22}, "1", "30", &animSpeed, 1.0f, 30.0f);
			DrawText(TextFormat("%.1f", animSpeed), 720, static_cast<int>(botBarY + 5), 14, th.textDim);

			if (GuiButton(Rectangle{770, botBarY, 110, 26}, "RESET GROUP")) {
				if (editorSelectedAnim >= 0 && editorSelectedAnim < static_cast<int>(animGroups.size())) {
					for (int fi : animGroups[editorSelectedAnim].frameIndices) {
						if (fi >= 0 && fi < static_cast<int>(frameRects.size()) && fi < static_cast<int>(originalFrameRects.size())) {
							frameRects[fi] = originalFrameRects[fi];
						}
					}
				}
			}
			if (GuiButton(Rectangle{890, botBarY, 90, 26}, "RESET ALL")) {
				for (size_t i = 0; i < frameRects.size() && i < originalFrameRects.size(); ++i) {
					frameRects[i] = originalFrameRects[i];
				}
			}
			}

			if (IsKeyPressed(KEY_Y)) editorPreviewZoom = std::max(0.1f, editorPreviewZoom - 0.1f);
			if (IsKeyPressed(KEY_E)) editorPreviewZoom = std::min(5.0f, editorPreviewZoom + 0.1f);

			bool anyTextEdit = frameNameEdit || frameXEdit || frameYEdit || frameWEdit || frameHEdit || frameFXEdit || frameFYEdit || frameFWEdit || frameFHEdit;
			if (!anyTextEdit && editorSelectedFrame >= 0 && editorSelectedFrame < static_cast<int>(frameRects.size())) {
				float spd = IsKeyDown(KEY_LEFT_SHIFT) ? 10.0f : 1.0f;
				if (IsKeyPressed(KEY_LEFT)) frameRects[editorSelectedFrame].x += spd;
				if (IsKeyPressed(KEY_RIGHT)) frameRects[editorSelectedFrame].x -= spd;
				if (IsKeyPressed(KEY_UP)) frameRects[editorSelectedFrame].y += spd;
				if (IsKeyPressed(KEY_DOWN)) frameRects[editorSelectedFrame].y -= spd;
			}

			if (IsKeyPressed(KEY_ESCAPE)) {
				appState = STATE_MAIN;
			}

			if (editorDragging) {
				if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
					Vector2 mouseImg = GetMousePosition();
					float dx = mouseImg.x - editorDragStart.x;
					float dy = mouseImg.y - editorDragStart.y;
					if (editorSelectedFrame >= 0 && editorSelectedFrame < static_cast<int>(frameRects.size())) {
						frameRects[editorSelectedFrame].x -= dx;
						frameRects[editorSelectedFrame].y -= dy;
					}
					editorDragStart = mouseImg;
					DrawText("Dragging frame... release to drop", 10, 700, 14, th.accent);
				} else {
					editorDragging = false;
				}
			}

		}

		EndDrawing();
	}

	CloseWindow();

	return 0;
}
