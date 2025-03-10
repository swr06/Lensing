#define USE_STACKLESS_TRAVERSAL

#define STRINGIFY(x) (#x)

#include "Pipeline.h"

#include "FpsCamera.h"
#include "GLClasses/Shader.h"
#include "Object.h"
#include "Entity.h"
#include "ModelFileLoader.h" 
#include "ModelRenderer.h"
#include "GLClasses/Fps.h"
#include "GLClasses/Framebuffer.h"
#include "ShaderManager.h"
#include "Utils/Timer.h"

#include "Physics/PhysicsApi.h"

#include "SphereLight.h"

#include "GLClasses/CubeTextureMap.h"
#include "Tonemap.h"


#include <string>


#include "BVH/BVHConstructor.h"
#include "BVH/Intersector.h"

#include "TAAJitter.h"

#include "Utility.h"

#include "Player.h"

#include "../Dependencies/imguizmo/ImGuizmo.h"

#include <implot.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// Externs.
int __TotalMeshesRendered = 0;
int __MainViewMeshesRendered = 0;

Candela::Player Player;
Candela::FPSCamera& Camera = Player.Camera;

static bool vsync = true;
static glm::vec3 _SunDirection = glm::vec3(0.1f, -1.0f, 0.1f);

// Timings
float CurrentTime = glfwGetTime();
float Frametime = 0.0f;
float DeltaTime = 0.0f;

bool StartRecording = false;
bool PlayingRecording = false;
int Playframe = 0;
std::vector<Candela::FPSCamera> RecordingCameras;

void WriteCamerasToFile(const std::vector<Candela::FPSCamera>& cameras, const std::string& filename) {
	std::ofstream outFile(filename, std::ios::binary);
	if (!outFile) {
		std::cerr << "Error opening file for writing: " << filename << '\n';
		return;
	}

	size_t count = cameras.size();
	outFile.write(reinterpret_cast<const char*>(&count), sizeof(count));  // Write vector size

	if (count > 0) {
		outFile.write(reinterpret_cast<const char*>(cameras.data()), count * sizeof(Camera));
	}

	outFile.close();
}

std::vector<Candela::FPSCamera> ReadCamerasFromFile(const std::string& filename) {
	std::ifstream inFile(filename, std::ios::binary);
	if (!inFile) {
		std::cerr << "Error opening file for reading: " << filename << '\n';
		return {};
	}

	size_t count;
	inFile.read(reinterpret_cast<char*>(&count), sizeof(count));  // Read vector size

	std::vector<Candela::FPSCamera> cameras(count);
	if (count > 0) {
		inFile.read(reinterpret_cast<char*>(cameras.data()), count * sizeof(Camera));
	}

	inFile.close();
	return cameras;
}

void SaveFBOToJPG(GLuint fbo, int width, int height, int frameNumber) {
	std::vector<unsigned char> pixels(width * height * 4);

	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	std::vector<unsigned char> flippedPixels(width * height * 4);

	for (int y = 0; y < height; ++y) {
		std::memcpy(&flippedPixels[y * width * 4], &pixels[(height - 1 - y) * width * 4], width * 4);
	}

	std::string filename = "Frames/" + std::to_string(frameNumber) + ".jpg";
	stbi_write_jpg(filename.c_str(), width, height, 4, flippedPixels.data(), width * 4);

	std::cout << "Saved frame to " << filename << std::endl;
}


static double RoundToNearest(double n, double x) {
	return round(n / x) * x;
}

bool WindowResizedThisFrame = false;

class RayTracerApp : public Candela::Application
{
public:

	RayTracerApp()
	{
		m_Width = 800;
		m_Height = 600;
	}

	void OnUserCreate(double ts) override
	{
	
	}

	void OnUserUpdate(double ts) override
	{
		glfwSwapInterval((int)vsync);

		GLFWwindow* window = GetWindow();
		float camera_speed = DeltaTime * 23.0f * ((glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS ? 3.0f : 1.0f));

		Player.OnUpdate(window, DeltaTime, camera_speed * 5.0, GetCurrentFrame());
	}

	void OnImguiRender(double ts) override
	{
		ImGui::Begin("--The CANDELA Engine--");
		ImGui::NewLine();
		ImGui::Text("FPS : %.2f", ImGui::GetIO().Framerate);
		ImGui::Text("Total Meshes Rendered : %d", __TotalMeshesRendered);
		ImGui::Text("Main View Meshes Rendered : %d", __MainViewMeshesRendered);
		ImGui::Text("Vsync : %s", vsync ? "On" : "Off");
		ImGui::Text("Window Size : %d x %d", GetWidth(), GetHeight());
		ImGui::Text("Camera Position : %f %f %f", Camera.GetPosition().x, Camera.GetPosition().y, Camera.GetPosition().z);
		ImGui::Text("Camera Front : %f %f %f", Camera.GetFront().x, Camera.GetFront().y, Camera.GetFront().z);

		if (StartRecording) {
			ImGui::NewLine();
			ImGui::NewLine();
			ImGui::Text("RECORDING...");
		}

		if (PlayingRecording) {
			ImGui::NewLine();
			ImGui::NewLine();
			ImGui::Text("PLAYING (and saving) RECORDING...");
		}

		ImGui::End();
	}

	void OnEvent(Candela::Event e) override
	{
		ImGuiIO& io = ImGui::GetIO();
		
		if (e.type == Candela::EventTypes::MouseMove && GetCursorLocked())
		{
			Camera.UpdateOnMouseMovement(e.mx, e.my);
		}


		if (e.type == Candela::EventTypes::MouseScroll && !ImGui::GetIO().WantCaptureMouse)
		{
			float Sign = e.msy < 0.0f ? 1.0f : -1.0f;
			Camera.SetFov(Camera.GetFov() + 2.0f * Sign);
			Camera.SetFov(glm::clamp(Camera.GetFov(), 1.0f, 89.0f));
		}

		if (e.type == Candela::EventTypes::WindowResize)
		{
			Camera.SetAspect((float)glm::max(e.wx, 1) / (float)glm::max(e.wy, 1));
			WindowResizedThisFrame = true && this->GetCurrentFrame() > 4;
		}

		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_ESCAPE) {
			exit(0);
		}

		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_F1)
		{
			this->SetCursorLocked(!this->GetCursorLocked());
		}

		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_F2)
		{
			Candela::ShaderManager::ForceRecompileShaders();
		}
		
		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_F3)
		{
			if (StartRecording) {
				StartRecording = false;
				WriteCamerasToFile(RecordingCameras, "data.bin");
			}
			else {
				RecordingCameras.clear();
				StartRecording = true;
			}
		}


		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_F4 && !StartRecording)
		{
			PlayingRecording = true;
			Playframe = 0;
		}

		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_V && this->GetCurrentFrame() > 5)
		{
			vsync = !vsync;
		}

		if (e.type == Candela::EventTypes::MousePress && !ImGui::GetIO().WantCaptureMouse)
		{
		}
	}


};


void Candela::StartPipeline()
{
	// Create App, initialize 
	RayTracerApp app;
	app.Initialize();
	app.SetCursorLocked(true);
	ImPlot::CreateContext();

	// Create VBO and VAO for drawing the screen-sized quad.
	GLClasses::VertexBuffer ScreenQuadVBO;
	GLClasses::VertexArray ScreenQuadVAO;
	GLClasses::CubeTextureMap Skymap;

	Skymap.CreateCubeTextureMap(
		{
		"Res/Skymap/px.png",
		"Res/Skymap/nx.png",
		"Res/Skymap/py.png",
		"Res/Skymap/ny.png",
		"Res/Skymap/pz.png",
		"Res/Skymap/nz.png"
		}, true
	);


	// Setup screensized quad for rendering
	{
		unsigned long long CurrentFrame = 0;
		float QuadVertices_NDC[] =
		{
			-1.0f,  1.0f,  0.0f, 1.0f, -1.0f, -1.0f,  0.0f, 0.0f,
			 1.0f, -1.0f,  1.0f, 0.0f, -1.0f,  1.0f,  0.0f, 1.0f,
			 1.0f, -1.0f,  1.0f, 0.0f,  1.0f,  1.0f,  1.0f, 1.0f
		};

		ScreenQuadVAO.Bind();
		ScreenQuadVBO.Bind();
		ScreenQuadVBO.BufferData(sizeof(QuadVertices_NDC), QuadVertices_NDC, GL_STATIC_DRAW);
		ScreenQuadVBO.VertexAttribPointer(0, 2, GL_FLOAT, 0, 4 * sizeof(GLfloat), 0);
		ScreenQuadVBO.VertexAttribPointer(1, 2, GL_FLOAT, 0, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));
		ScreenQuadVAO.Unbind();
	}

	// Create Shaders
	ShaderManager::CreateShaders();

	GLClasses::Shader& BlitShader = ShaderManager::GetShader("BASIC_BLIT");
	GLClasses::Shader& RenderShader = ShaderManager::GetShader("RENDER");

	// Matrices
	glm::mat4 PreviousView;
	glm::mat4 PreviousProjection;
	glm::mat4 View;
	glm::mat4 Projection;
	glm::mat4 InverseView;
	glm::mat4 InverseProjection;

	GLClasses::Framebuffer RecordingFBO(1920, 1080, { GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, true, true }, true, false);

	while (!glfwWindowShouldClose(app.GetWindow()))
	{
		// Window is minimized.
		if (glfwGetWindowAttrib(app.GetWindow(), GLFW_ICONIFIED)) {
			app.OnUpdate();
			app.FinishFrame();
			GLClasses::DisplayFrameRate(app.GetWindow(), "Candela ");
			continue;
		}

		app.OnUpdate();

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		glDisable(GL_CULL_FACE);

		if (StartRecording) {
			RecordingCameras.push_back(Camera);
		}

		Camera.Timestamp = glfwGetTime();

		// Blit! 

		// Play recording 
		if (PlayingRecording && RecordingCameras.size()) {
			Candela::FPSCamera* c = &Camera;
			*c = RecordingCameras[Playframe % RecordingCameras.size()];
			Playframe += 1;
		}


		RecordingFBO.Bind();

		glDisable(GL_CULL_FACE);

		RenderShader.Use();

		RenderShader.SetFloat("u_Time", Camera.Timestamp);
		RenderShader.SetInteger("u_Skymap", 2);
		RenderShader.SetMatrix4("u_ViewProjection", Camera.GetViewProjection());
		RenderShader.SetMatrix4("u_Projection", Camera.GetProjectionMatrix());
		RenderShader.SetMatrix4("u_View", Camera.GetViewMatrix());
		RenderShader.SetMatrix4("u_InverseProjection", glm::inverse(Camera.GetProjectionMatrix()));
		RenderShader.SetMatrix4("u_InverseView", glm::inverse(Camera.GetViewMatrix()));
		RenderShader.SetFloat("u_Width", float(app.GetWidth()));
		RenderShader.SetFloat("u_Height", float(app.GetHeight()));

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_CUBE_MAP, Skymap.GetID());

		ScreenQuadVAO.Bind();
		glDrawArrays(GL_TRIANGLES, 0, 6);
		ScreenQuadVAO.Unbind();

		RecordingFBO.Unbind();

		// Render to screen
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, app.GetWidth(), app.GetHeight());

		BlitShader.Use();
		BlitShader.SetInteger("u_Texture", 0);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, RecordingFBO.GetTexture());

		ScreenQuadVAO.Bind();
		glDrawArrays(GL_TRIANGLES, 0, 6);
		ScreenQuadVAO.Unbind();

		if (PlayingRecording) {
			SaveFBOToJPG(RecordingFBO.GetFramebuffer(), 1920, 1080, Playframe);
		}

		// Finish 

		glFinish();
		app.FinishFrame();

		CurrentTime = glfwGetTime();
		DeltaTime = CurrentTime - Frametime;
		Frametime = glfwGetTime();

		if (app.GetCurrentFrame() > 4 && WindowResizedThisFrame) {
			WindowResizedThisFrame = false;
		}

		std::string Title = "Candela | " + std::to_string(glfwGetTime()) + " | ";
		if (PlayingRecording) {
			Title += "Playing Recording, frame : " + std::to_string(Playframe) + " | " + (std::to_string(100.*float(Playframe)/float(RecordingCameras.size())) + "% | ");
		}
		else if (StartRecording) {
			Title += "Recording, frame : " + std::to_string(RecordingCameras.size()) + " | ";
		}
		GLClasses::DisplayFrameRate(app.GetWindow(), Title);
		
	}


}

// End.