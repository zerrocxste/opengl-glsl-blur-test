#include <iostream>
#include <Windows.h>
#include <vector>
#include <tuple>

#include "gl3w/glcorearb.h"
#include "gl3w/gl3winit.h"
#include "gl3w/gl3w.h"

#include <gl\GL.h>
#pragma comment(lib, "OpenGL32.lib")

#include <gl/Glu.h>
#pragma comment (lib, "Glu32.lib")

extern "C" 
{
	WINGDIAPI void APIENTRY glTexCoord2f(GLfloat s, GLfloat t);
	WINGDIAPI void APIENTRY glPushMatrix(void);
	WINGDIAPI void APIENTRY glPopMatrix(void);
	WINGDIAPI void APIENTRY glBegin(GLenum mode);
	WINGDIAPI void APIENTRY glVertex2f(GLfloat x, GLfloat y);
	WINGDIAPI void APIENTRY glEnableClientState(GLenum array);
	WINGDIAPI void APIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer);
	WINGDIAPI void APIENTRY glColor3f(GLfloat red, GLfloat green, GLfloat blue);
	WINGDIAPI void APIENTRY glDisableClientState(GLenum array);
	WINGDIAPI void APIENTRY glEnd(void);
	WINGDIAPI void APIENTRY glMatrixMode(GLenum mode);
	WINGDIAPI void APIENTRY glLoadIdentity(void);
	WINGDIAPI void APIENTRY glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
	WINGDIAPI void APIENTRY glShadeModel(GLenum mode);
}

#define GL_BGR_EXT                        0x80E0

#define GL_MODELVIEW                      0x1700
#define GL_PROJECTION                     0x1701
#define GL_TEXTURE                        0x1702

#define GL_FLAT                           0x1D00
#define GL_SMOOTH                         0x1D01

#define GL_ALPHA_TEST                     0x0BC0

#define GL_CLAMP                          0x2900
#define GL_REPEAT                         0x2901

const int WIDTH = 1024;
const int HEIGHT = 768;

struct screenshot_data
{
	char* pScreenshotMap;
	int iScreenshotSize;
	int iScreenshotWidth;
	int iScreenshotHeight;
};

screenshot_data CreateScreenshot()
{
	screenshot_data screenshot{};

	auto hDC = GetDC(0);

	auto Width = GetDeviceCaps(hDC, HORZRES);
	auto Height = GetDeviceCaps(hDC, VERTRES);

	auto CompressWidth = 500;
	auto CompressHeight = 500;

	CompressWidth = Width /*/ 2.5f*/;
	CompressHeight = Height /*/ 2.5f*/;

	printf("%x %x | %d %d\n", GetDesktopWindow(), hDC, Width, Height);

	auto hMemDC = CreateCompatibleDC(hDC);
	auto hBitmap = CreateCompatibleBitmap(hDC, CompressWidth, CompressHeight);
	auto hOld = SelectObject(hMemDC, hBitmap);
	SetStretchBltMode(hMemDC, HALFTONE);
	StretchBlt(hMemDC, 0, 0, CompressWidth, CompressHeight, hDC, 0, 0, Width, Height, SRCCOPY);

	BITMAPINFO MyBMInfo = { 0 };
	MyBMInfo.bmiHeader.biSize = sizeof(MyBMInfo.bmiHeader);

	SelectObject(hMemDC, hOld);
	GetDIBits(hDC, hBitmap, 0, 0, NULL, &MyBMInfo, DIB_RGB_COLORS);
	MyBMInfo.bmiHeader.biCompression = BI_RGB;
	MyBMInfo.bmiHeader.biBitCount = 24;
	screenshot.pScreenshotMap = new char[screenshot.iScreenshotSize = MyBMInfo.bmiHeader.biSizeImage];
	GetDIBits(hDC, hBitmap, 0, MyBMInfo.bmiHeader.biHeight, (LPVOID)screenshot.pScreenshotMap, &MyBMInfo, DIB_RGB_COLORS);

	screenshot.iScreenshotWidth = CompressWidth;
	screenshot.iScreenshotHeight = CompressHeight;

	DeleteDC(hMemDC);
	ReleaseDC(0, hDC);
	DeleteObject(hBitmap);

	printf("Width: (%d) Height: (%d) Compressed Width: (%d) Compressed Height (%d) Size: %lf mb\n", Width, Height, CompressWidth, CompressHeight, (double)screenshot.iScreenshotSize / 1000. / 1000.);
	return screenshot;
}

GLuint LoadTextureBytes(const unsigned char* pszBitmapBytes, int Width, int Height, int TextureMode)
{
	GLuint ret;

	GLint last_texture;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
	glGenTextures(1, &ret);
	glBindTexture(GL_TEXTURE_2D, ret);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, Width, Height, 0, TextureMode, GL_UNSIGNED_BYTE, pszBitmapBytes);
	glBindTexture(GL_TEXTURE_2D, last_texture);

	return ret;
}

void LoadNextImage();

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void RenderScene();

HWND hWnd;
HDC hDC;
HGLRC hRC;
bool quit = false;
GLuint vertexShader;
GLuint fragmentShader;
GLuint shaderProgram;
GLuint vertexBuffer;
GLuint indexBuffer;
GLuint texture;

GLuint vertexBufferAlphaShader;
GLuint indexBufferAlphaShader;
GLuint alphaChannelShader;

void Cleanup()
{
	glDeleteProgram(shaderProgram);
	glDeleteBuffers(1, &vertexBuffer);
	glDeleteBuffers(1, &indexBuffer);
	glDeleteTextures(1, &texture);

	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(hRC);
	ReleaseDC(hWnd, hDC);
	DestroyWindow(hWnd);
	UnregisterClass(L"opengltest", GetModuleHandle(NULL));
}

GLint textureBackup = 0;
GLint fboBackup = 0;
GLint programBackup = 0;

GLuint frameBuffer = 0;

bool CompileTypeShader(const char* pszShader, GLint ShaderType, GLuint& Shader, char*& pszCompilationStatus)
{
	GLint iCompileStatus;
	GLint iInfoLogLength;

	Shader = glCreateShader(ShaderType);
	auto iVtxShaderLen = strlen(pszShader);
	glShaderSource(Shader, 1, &pszShader, (GLint*)&iVtxShaderLen);
	glCompileShader(Shader);
	iCompileStatus = 0;
	glGetShaderiv(Shader, GL_COMPILE_STATUS, &iCompileStatus);
	if (iCompileStatus == GL_FALSE)
	{
		iInfoLogLength = 0;
		glGetShaderiv(Shader, GL_INFO_LOG_LENGTH, &iInfoLogLength);
		pszCompilationStatus = new char[iInfoLogLength]();
		glGetShaderInfoLog(Shader, iInfoLogLength, &iInfoLogLength, pszCompilationStatus);
		DebugBreak();
		return false;
	}

	return true;
}

bool CreateProgramm(const char* szVtxShader, const char* szFragShader, GLuint& program)
{
	GLuint vtxShader;
	GLuint fragShader;
	char* pszLog;

	pszLog = nullptr;
	if (!CompileTypeShader(szVtxShader, GL_VERTEX_SHADER, vtxShader, pszLog))
	{
		printf(__FUNCTION__ "(Vertex shader) > %s\n", pszLog);
		MessageBoxA(NULL, "See console", __FUNCTION__, MB_OK);
		delete[] pszLog;
		return false;
	}

	pszLog = nullptr;
	if (!CompileTypeShader(szFragShader, GL_FRAGMENT_SHADER, fragShader, pszLog))
	{
		printf(__FUNCTION__ "(Fragment shader) > %s\n", pszLog);
		MessageBoxA(NULL, "See console", __FUNCTION__, MB_OK);
		delete[] pszLog;
		return false;
	}

	program = glCreateProgram();
	glAttachShader(program, vtxShader);
	glAttachShader(program, fragShader);
	glLinkProgram(program);
	
	glDeleteShader(vtxShader);
	glDeleteShader(fragShader);

	return true;
}

void Initialize()
{
	PIXELFORMATDESCRIPTOR pfd = { 0 };
	pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DOUBLEBUFFER | PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.cStencilBits = 8;
	pfd.iLayerType = PFD_MAIN_PLANE;

	hDC = GetDC(hWnd);
	int pixelFormat = ChoosePixelFormat(hDC, &pfd);
	SetPixelFormat(hDC, pixelFormat, &pfd);
	hRC = wglCreateContext(hDC);
	wglMakeCurrent(hDC, hRC);

	GL3::Initialize();

	glViewport(0.f, 0.f, WIDTH, HEIGHT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D(0, WIDTH, 0, HEIGHT);

	// Загрузка шейдеров
	const char* vertexShaderSource = R"(
		#version 130
		#extension GL_ARB_explicit_attrib_location : require
		#extension GL_ARB_explicit_uniform_location : require
		layout (location = 0) in vec2 position;
		layout (location = 1) in vec2 texCoord;
		out vec2 fragTexCoord;
		void main()
		{
			gl_Position = vec4(position, 0.0, 1.0);
			fragTexCoord = texCoord;
		}
	)";

	const char* fragmentShaderSource = R"(
		#version 130
		in vec2 fragTexCoord;
		out vec4 color;
		uniform sampler2D tex;
		uniform float blur_radius;
		uniform float cr_len; 

		#define LEFT_BOTTOM_CORNER 0
		#define LEFT_UP_CORNER 1
		#define RIGHT_UP_CORNER 2
		#define RIGHT_BOTTOM_CORNER 3
	
		void draw_corner(float from_point_length, int corner)
		{
			vec2 texelSize = 1.0 / textureSize(tex, 0);
			vec2 sides_difference = vec2(texelSize.x / texelSize.y, texelSize.y / texelSize.x);
			
			vec2 norm_pos = vec2(fragTexCoord.x * sides_difference.y, fragTexCoord.y * sides_difference.x);	

			float len = 0.0;

			if (corner == LEFT_BOTTOM_CORNER)
				len = length(vec2(from_point_length, from_point_length) - norm_pos);
			if (corner == LEFT_UP_CORNER)
				len = length(vec2(from_point_length, (1.0 * sides_difference.x) - from_point_length) - norm_pos);
			if (corner == RIGHT_UP_CORNER)
				len = length(vec2((1.0 * sides_difference.y) - from_point_length, (1.0 * sides_difference.x) - from_point_length) - norm_pos);
			if (corner == RIGHT_BOTTOM_CORNER)
				len = length(vec2((1.0 * sides_difference.y) - from_point_length, from_point_length) - norm_pos);

			if (len > from_point_length)
			{
				color.a = 0.0;
			}
		}

		void main()
		{
			vec2 texelSize = 1.0 / textureSize(tex, 0);

			vec4 sum = vec4(0.0);

			for (float x = -blur_radius; x <= blur_radius; x++) {
				for (float y = -blur_radius; y <= blur_radius; y++) {
					vec2 offset = vec2(float(x), float(y)) * texelSize;
					sum += texture(tex, fragTexCoord + offset);
				}
			}

			float dt = (2.0 * blur_radius + 1.0);    

			color = sum / (dt * dt);

			vec2 sides_difference = vec2(texelSize.x / texelSize.y, texelSize.y / texelSize.x);

			if (fragTexCoord.x < (cr_len * sides_difference.x) && fragTexCoord.y < (cr_len * sides_difference.y))
				draw_corner(cr_len, LEFT_BOTTOM_CORNER);

			if (fragTexCoord.x < (cr_len * sides_difference.x) && fragTexCoord.y > (1.0 - cr_len * sides_difference.y))
				draw_corner(cr_len, LEFT_UP_CORNER);

			if (fragTexCoord.x > (1.0 - cr_len * sides_difference.x) && fragTexCoord.y > (1.0 - cr_len * sides_difference.y))
				draw_corner(cr_len, RIGHT_UP_CORNER);

			if (fragTexCoord.x > (1.0 - cr_len * sides_difference.x) && fragTexCoord.y < (cr_len * sides_difference.y))
				draw_corner(cr_len, RIGHT_BOTTOM_CORNER);
		}
	)";

	CreateProgramm(vertexShaderSource, fragmentShaderSource, shaderProgram);

	std::vector<float> vertices = {
		-1.f, -1.f,			0.0f, 0.0f,
		1.f, -1.f,			1.f , 0.0f,
		1.f, 1.f,			1.f , 1.f,
		-1.f, 1.f,			0.0f, 1.f
	};

	glGenBuffers(1, &vertexBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

	std::vector<unsigned int> indices = {
		0, 1, 2,
		2, 3, 0
	};

	glGenBuffers(1, &indexBuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
}

void qube(float x, float y, float w, float h, std::vector<float> color)
{
	glBegin(GL_QUADS);
	glColor3f(color[0], color[1], color[2]);
	glVertex2f(x, y);
	glVertex2f(x + w, y);
	glVertex2f(x + w, y + h);
	glVertex2f(x, y + h);
	glEnd();
}

void RenderScene()
{
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glClearColor(0.f, 0.f, 0.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);

	const float offset = 0.f;

	qube(20.f, 20.f, WIDTH - (20.f * 2.f), HEIGHT - (20.f * 2.f), { 0.f, 1.f, 1.f });

	float size_x = 250.f, size_y = 250.f;
	qube(WIDTH / 2 - size_x, HEIGHT / 2, size_x, size_y, { 1.f, 0.f, 0.f });
	qube(WIDTH / 2, HEIGHT / 2, size_x, size_y, { 0.f, 1.f, 0.f });
	qube(WIDTH / 2 - size_x, HEIGHT / 2 - size_y, size_x, size_y, { 0.f, 0.f, 1.f });
	qube(WIDTH / 2, HEIGHT / 2 - size_y, size_x, size_y, { 1.f, 1.f, 0.f });

	if (texture != 0)
		glDeleteTextures(1, &texture);

#define DESKTOP_SCREEN 2

#if DESKTOP_SCREEN == 1
	auto screen = CreateScreenshot();

	texture = LoadTextureBytes(
		(unsigned char*)screen.pScreenshotMap,
		screen.iScreenshotWidth, screen.iScreenshotHeight,
		GL_RGB);

	delete[] screen.pScreenshotMap;

	glUseProgram(shaderProgram);

	glEnable(GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glUniform1i(glGetUniformLocation(shaderProgram, "tex"), 0);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);

	glDisable(GL_TEXTURE_2D);

	glUseProgram(0);
#elif DESKTOP_SCREEN == 2
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	glBindTexture(GL_TEXTURE_2D, texture);
	glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, WIDTH, HEIGHT, 0);

	/*static GLuint framebuffer = 0;
	if (!framebuffer)
		glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);*/

	glUseProgram(shaderProgram);

	glEnable(GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glUniform1i(glGetUniformLocation(shaderProgram, "tex"), 0);
	glUniform1f(glGetUniformLocation(shaderProgram, "cr_len"), 0.15f);
	glUniform1f(glGetUniformLocation(shaderProgram, "blur_radius"), 10.f);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);

	glDisable(GL_TEXTURE_2D);

	//glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glUseProgram(0);

	/*glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texture);
	glBegin(GL_QUADS);
	glColor3f(1.f, 1.f, 1.f);
	glTexCoord2f(0.0f, 0.0f); glVertex2f(200.0f, 200.0f);
	glTexCoord2f(1.0f, 0.0f); glVertex2f(WIDTH - 200.f, 200.0f);
	glTexCoord2f(1.0f, 1.0f); glVertex2f(WIDTH - 200.f, HEIGHT - 200.f);
	glTexCoord2f(0.0f, 1.0f); glVertex2f(200.0f, HEIGHT - 200.f);
	glEnd();
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_TEXTURE_2D);*/
#endif

	SwapBuffers(hDC);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	FILE* output_stream = nullptr;
	if (AllocConsole())
	{
		freopen_s(&output_stream, "conout$", "w", stdout);
	}

	WNDCLASSEX wc = { 0 };
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = L"opengltest";
	RegisterClassEx(&wc);

	hWnd = CreateWindowEx(WS_EX_APPWINDOW, L"opengltest", L"opengl test", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, WIDTH + 50, HEIGHT + 50, NULL, NULL, hInstance, NULL);

	Initialize();
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	MSG msg;
	while (!quit)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				break;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}

		RenderScene();
	}

	Cleanup();
	return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CLOSE:
		quit = true;
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
}