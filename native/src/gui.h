// In-game settings editor (gui.cpp).
#pragma once

#include "ts.h"

void GuiRender();      // draws the editor; called from RenderFrame while it is open
void GuiOnOpened();    // reloads the document from config.toml
bool GuiCapturingKeys();
bool GuiWantsTextInput();
void GuiCaptureKey(int vk, bool pressed);
void GuiOnKey(int glfwKey, int action);
void GuiOnChar(unsigned int c);
void GuiOnMouseButton(int button, int action);
void GuiOnCursor(double fbX, double fbY);
void GuiOnScroll(double dx, double dy);
