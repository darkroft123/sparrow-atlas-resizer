#include <cmath>
#include <cstdio>
#include <iostream>
#include <ctgmath>
#include <vector>

#include "pugixml.hpp"
#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include "raymath.h"

#include "dialogs.h"

bool stringEndsWith (std::string const &fullString, std::string const &ending) {
	if (fullString.length() >= ending.length()) {
		return (0 == fullString.compare (fullString.length() - ending.length(), ending.length(), ending));
	}
	return false;
}

bool loadAtlas(Texture &spritesheet, Rectangle &imageDimensions, pugi::xml_document &doc, std::vector<Rectangle> &frameRects, const std::string &pngPath) {
	UnloadTexture(spritesheet);

	spritesheet = LoadTexture(pngPath.c_str());
	SetTextureFilter(spritesheet, TEXTURE_FILTER_BILINEAR);
	imageDimensions = Rectangle{.x = 0, .y = 0, .width = static_cast<float>(spritesheet.width), .height = static_cast<float>(spritesheet.height)};

	std::string xmlPath = pngPath;
	const pugi::xml_parse_result result = doc.load_file(xmlPath.replace(xmlPath.length() - 3, 3, "xml").c_str());

	frameRects.clear();

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
	}
	return true;
}

void saveOutput(const Texture &spritesheet, const pugi::xml_document &doc, const std::string &pngPath, const std::string &xmlPath, float scale) {
	Image output = LoadImageFromTexture(spritesheet);
	ImageResize(&output, static_cast<int>(std::round(static_cast<float>(output.width) * scale)), static_cast<int>((static_cast<float>(output.height) * scale)));

	if (!ExportImage(output, pngPath.c_str())) {
		TraceLog(LOG_ERROR, "Failed to save PNG file.");
	}

	pugi::xml_document outDoc;
	outDoc.load_file(xmlPath.c_str());

	const float imageWidth = outDoc.child("TextureAtlas").attribute("width").as_float();
	const float imageHeight = outDoc.child("TextureAtlas").attribute("height").as_float();

	outDoc.child("TextureAtlas").attribute("width").set_value((imageWidth * scale));
	outDoc.child("TextureAtlas").attribute("height").set_value((imageHeight * scale));

	for (auto frame : outDoc.child("TextureAtlas").children("SubTexture")) {
		const float x = frame.attribute("x").as_float();
		const float y = frame.attribute("y").as_float();
		const float width = frame.attribute("width").as_float();
		const float height = frame.attribute("height").as_float();

		const float frameX = frame.attribute("frameX").as_float();
		const float frameY = frame.attribute("frameY").as_float();
		const float frameWidth = frame.attribute("frameWidth").as_float();
		const float frameHeight = frame.attribute("frameHeight").as_float();

		frame.attribute("x").set_value((x * scale));
		frame.attribute("y").set_value((y * scale));
		frame.attribute("width").set_value((width * scale));
		frame.attribute("height").set_value((height * scale));

		frame.attribute("frameX").set_value((frameX * scale));
		frame.attribute("frameY").set_value((frameY * scale));
		frame.attribute("frameWidth").set_value((frameWidth * scale));
		frame.attribute("frameHeight").set_value((frameHeight * scale));
	}

	if (!outDoc.save_file(xmlPath.c_str())) {
		TraceLog(LOG_ERROR, "Failed to save XML file.");
	}
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
					loadAtlas(spritesheet, imageDimensions, doc, frameRects, droppedFiles.paths[i]);
				}
			}
			UnloadDroppedFiles(droppedFiles);
		}

		if (!IsKeyDown(KEY_LEFT_CONTROL)) {
			constexpr float minZoom = 0.1f;
			constexpr float maxZoom = 2.0f;
			camSpritesheet.zoom = Clamp(camSpritesheet.zoom + GetMouseWheelMove() * 0.1f, minZoom, maxZoom);
		}
		else {
			scale = Clamp(scale + GetMouseWheelMove() * 0.1f, minScale, maxScale);
		}

		if (spritesheet.width <= 0) {
			const auto message = "Drag an image to begin.";
			auto [x, y] = MeasureTextEx(GetFontDefault(), message, 32, 1);
			DrawText(message, static_cast<int>(static_cast<float>(GetRenderWidth()) - x) / 2, static_cast<int>(static_cast<float>(GetRenderHeight()) - y) / 2, 32, BLACK);
		}
		else {
		BeginMode2D(camSpritesheet);
			DrawTexturePro(spritesheet, imageDimensions, Rectangle{
							   .x = 0, .y = 0, .width = imageDimensions.width,
							   .height = imageDimensions.height
						   }, Vector2Zero(), 0.0f, ColorAlpha(WHITE, 0.5));
			DrawRectangleLines(0, 0, static_cast<int>(imageDimensions.width), static_cast<int>(imageDimensions.height), RED);

			for (const auto rect : frameRects) {
				DrawRectanglePro(rect, Vector2Zero(), 0.0, ColorAlpha(BLUE, 0.05));
			}

			DrawTexturePro(spritesheet, imageDimensions, Rectangle{
				   .x = 0, .y = 0, .width = imageDimensions.width * scale,
				   .height = imageDimensions.height * scale
			   }, Vector2Zero(), 0.0f, WHITE);
			DrawRectangleLines(0, 0, static_cast<int>(imageDimensions.width * scale), static_cast<int>(imageDimensions.height * scale), GREEN);
			for (const auto [x, y, width, height] : frameRects) {
				DrawRectanglePro(Rectangle{.x = x * scale, .y = y * scale, .width = width * scale, .height = height * scale}, Vector2Zero(), 0.0, ColorAlpha(BLUE, 0.1));
			}
		EndMode2D();

		}


		DrawText(TextFormat("Scale: %.03lf", scale), 1000, 12, 32, BLACK);
		GuiSlider(Rectangle{.x = 1000, .y = 50, .width = 250, .height = 25}, TextFormat("%0.1lf", minScale), TextFormat("%0.1lf", maxScale), &scale, minScale, maxScale);
		GuiValueBoxFloat(Rectangle{.x = 1260, .y = 50, .width = 60, .height = 25}, NULL, scaleText, &scale, scaleEditMode);
		if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), Rectangle{.x = 1260, .y = 50, .width = 60, .height = 25})) {
			scaleEditMode = !scaleEditMode;
		}
		if (!scaleEditMode) {
			snprintf(scaleText, sizeof(scaleText), "%.3f", scale);
		}

		DrawText(TextFormat("Camera Zoom: %.01lf (mouse wheel)", camSpritesheet.zoom), 900, 690, 24, BLACK);

		if (GuiButton(Rectangle{.x = 1000, .y = 100, .width = 250, .height = 25}, "SAVE")) {
			saveOutput(spritesheet, doc, "output.png", "output.xml", scale);
		}

		if (GuiButton(Rectangle{.x = 1000, .y = 135, .width = 250, .height = 25}, "CHANGE PNG")) {
			std::string pngPath = openFileDlg("PNG Files (*.png)\0*.png\0");
			if (!pngPath.empty()) {
				loadAtlas(spritesheet, imageDimensions, doc, frameRects, pngPath);
				scale = 1.0f;
			}
		}

		if (GuiButton(Rectangle{.x = 1000, .y = 170, .width = 250, .height = 25}, "SAVE AS")) {
			std::string pngPath = saveFileDlg("PNG Files (*.png)\0*.png\0");
			if (!pngPath.empty()) {
				std::string xmlPath = pngPath;
				xmlPath.replace(xmlPath.length() - 3, 3, "xml");
				saveOutput(spritesheet, doc, pngPath, xmlPath, scale);
			}
		}
		EndDrawing();
	}

	CloseWindow();

	return 0;
}