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
		ImGui::Begin("Candela");
		ImGui::Text("FPS : %.2f", ImGui::GetIO().Framerate);
		ImGui::Text("Total Meshes Rendered : %d", __TotalMeshesRendered);
		ImGui::Text("Main View Meshes Rendered : %d", __MainViewMeshesRendered);
		ImGui::Text("Vsync : %s", vsync ? "On" : "Off");
		ImGui::Text("Window Size : %d x %d", GetWidth(), GetHeight());
		ImGui::Text("Camera Position : %f %f %f", Camera.GetPosition().x, Camera.GetPosition().y, Camera.GetPosition().z);
		ImGui::Text("Camera Front : %f %f %f", Camera.GetFront().x, Camera.GetFront().y, Camera.GetFront().z);
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

	GLClasses::Shader& BasicBlitShader = ShaderManager::GetShader("BASIC_BLIT");

	// Matrices
	glm::mat4 PreviousView;
	glm::mat4 PreviousProjection;
	glm::mat4 View;
	glm::mat4 Projection;
	glm::mat4 InverseView;
	glm::mat4 InverseProjection;
	
	

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


		// Blit! 

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, app.GetWidth(), app.GetHeight());

		BasicBlitShader.Use();

		BasicBlitShader.SetFloat("u_Time", glfwGetTime());
		BasicBlitShader.SetInteger("u_Frame", app.GetCurrentFrame());
		BasicBlitShader.SetInteger("u_Skymap", 2);
		BasicBlitShader.SetInteger("u_CurrentFrame", app.GetCurrentFrame());
		BasicBlitShader.SetMatrix4("u_ViewProjection", Camera.GetViewProjection());
		BasicBlitShader.SetMatrix4("u_Projection", Camera.GetProjectionMatrix());
		BasicBlitShader.SetMatrix4("u_View", Camera.GetViewMatrix());
		BasicBlitShader.SetMatrix4("u_InverseProjection", glm::inverse(Camera.GetProjectionMatrix()));
		BasicBlitShader.SetMatrix4("u_InverseView", glm::inverse(Camera.GetViewMatrix()));
		BasicBlitShader.SetFloat("u_Width", float(app.GetWidth()));
		BasicBlitShader.SetFloat("u_Height", float(app.GetHeight()));

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_CUBE_MAP, Skymap.GetID());

		ScreenQuadVAO.Bind();
		glDrawArrays(GL_TRIANGLES, 0, 6);
		ScreenQuadVAO.Unbind();

		// Finish 

		glFinish();
		app.FinishFrame();

		CurrentTime = glfwGetTime();
		DeltaTime = CurrentTime - Frametime;
		Frametime = glfwGetTime();

		if (app.GetCurrentFrame() > 4 && WindowResizedThisFrame) {
			WindowResizedThisFrame = false;
		}

		GLClasses::DisplayFrameRate(app.GetWindow(), "Candela");
		
	}
}

// End.