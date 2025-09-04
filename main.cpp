#include <algorithm>
#include <array>
#include <chrono>
#include <GL/glew.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <iostream>
#include <GL/glu.h>
#include <GLFW/glfw3.h>
#include <optional>
#include <stdio.h>
#include <vector>
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

struct FRAME
{
  FRAME(GLuint framebuffer, GLuint texture, uint64_t time) :
    framebuffer_(framebuffer),
    texture_(texture),
    time_(time)
  {
  }

  GLuint framebuffer_;
  GLuint texture_;
  uint64_t time_;
};

int main(int argc, char** argv)
{
  // Check command line arguments
  if (argc != 2)
  {
    std::cerr << "Usage:\nImGuiPlayer video.mp4" << std::endl;
    return -1;
  }
  // Parse file with FFMPEG
  AVFormatContext* format_context = nullptr;
  if (avformat_open_input(&format_context, argv[1], nullptr, nullptr) < 0)
  {
    std::cerr << "Failed to open file" << std::endl;
    return -1;
  }
  if (avformat_find_stream_info(format_context, nullptr) < 0)
  {
    std::cerr << "Failed to find stream info" << std::endl;
    return -1;
  }
  std::optional<unsigned int> video_stream;
  for (unsigned int i = 0; i < format_context->nb_streams; i++)
  {
    if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    {
      video_stream = i;
      break;
    }
  }
  if (!video_stream.has_value())
  {
    std::cerr << "Failed to find video stream" << std::endl;
    return -1;
  }
  // Open the decoder
  const AVCodec* codec = avcodec_find_decoder(format_context->streams[*video_stream]->codecpar->codec_id);
  if (codec == nullptr)
  {
    std::cerr << "Failed to find decoder" << std::endl;
    return -1;
  }
  AVCodecContext* codec_context = avcodec_alloc_context3(codec);
  if (codec_context == nullptr)
  {
    std::cerr << "Failed to allocate codec context" << std::endl;
    return -1;
  }
  // Copy codec parameters
  if (avcodec_parameters_to_context(codec_context, format_context->streams[*video_stream]->codecpar) < 0)
  {
    std::cerr << "Failed to copy codec parameters" << std::endl;
    return -1;
  }
  if (avcodec_open2(codec_context, codec, nullptr) < 0)
  {
    std::cerr << "Failed to open codec" << std::endl;
    return -1;
  }
  // Init window
  if (!glfwInit())
  {
    printf("Failed to initialize GLFW\n");
    return -1;
  }
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  const int width = format_context->streams[*video_stream]->codecpar->width;
  const int height = format_context->streams[*video_stream]->codecpar->height;
  GLFWwindow* window = glfwCreateWindow(width, height, "ImGui Player", NULL, NULL);
  if (!window)
  {
    glfwTerminate();
    return -1;
  }
  glfwMakeContextCurrent(window);
  if (glewInit() != GLEW_OK)
  {
    printf("Failed to initialize GLEW\n");
    return -1;
  }
  glfwSwapInterval(1);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  int display_w, display_h;
  glfwGetFramebufferSize(window, &display_w, &display_h);
  glViewport(0, 0, display_w, display_h);
  // Setup ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO(); (void)io;
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330 core");
  // Shaders
  const char* yuv_vertex_shader_source = R"(#version 330 core
                                            layout(location = 0) in vec2 in_pos;
                                            layout(location = 1) in vec2 in_tex_coord;
                                            out vec2 tex_coord;
                                            void main()
                                            {
                                                gl_Position = vec4(in_pos, 0.0, 1.0);
                                                tex_coord = in_tex_coord;
                                            })";
  const char* yuv_fragment_shader_source = R"(#version 330 core
                                              out vec4 FragColor;
                                              in vec2 tex_coord;
                                              uniform sampler2D texture_y;
                                              uniform sampler2D texture_u;
                                              uniform sampler2D texture_v;
                                              void main()
                                              {
                                                  float y = texture(texture_y, tex_coord).r;
                                                  float u = texture(texture_u, tex_coord).r - 0.5;
                                                  float v = texture(texture_v, tex_coord).r - 0.5;
                                                  vec3 rgb = mat3(1.0, 1.0, 1.0,
                                                                  0.0, -0.39465, 2.03211,
                                                                  1.13983, -0.58060, 0.0) * vec3(y, u, v);
                                                  FragColor = vec4(rgb, 1.0);
                                              })";
  const GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertex_shader, 1, &yuv_vertex_shader_source, nullptr);
  glCompileShader(vertex_shader);
  const GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragment_shader, 1, &yuv_fragment_shader_source, nullptr);
  glCompileShader(fragment_shader);
  GLuint yuv_shader_program = glCreateProgram();
  glAttachShader(yuv_shader_program, vertex_shader);
  glAttachShader(yuv_shader_program, fragment_shader);
  glLinkProgram(yuv_shader_program);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
  // YUV textures
  std::array<GLuint, 3> yuv_textures;
  glGenTextures(3, yuv_textures.data());
  for (int i = 0; i < 3; i++)
  {
    glBindTexture(GL_TEXTURE_2D, yuv_textures[i]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glBindTexture(GL_TEXTURE_2D, yuv_textures[0]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
  glBindTexture(GL_TEXTURE_2D, yuv_textures[1]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width / 2, height / 2, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
  glBindTexture(GL_TEXTURE_2D, yuv_textures[2]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width / 2, height / 2, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
  // Frame buffers
  std::vector<FRAME> frames;
  std::vector<FRAME> free_frames;
  for (int i = 0; i < 5; ++i)
  {
    GLuint frame_buffer = GL_INVALID_VALUE;
    glGenFramebuffers(1, &frame_buffer);
    glBindFramebuffer(GL_FRAMEBUFFER, frame_buffer);
    GLuint texture = GL_INVALID_VALUE;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      throw std::runtime_error("Framebuffer is not complete!");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    free_frames.push_back(FRAME(frame_buffer, texture, 0));
  }
  // Geometory
  const float yuv_vertices[] =
  {
    // positions  texture coords
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
    -1.0f,  1.0f, 0.0f, 1.0f
  };
  const unsigned int yuv_indices[] =
  {
    0, 1, 2,
    2, 3, 0
  };
  GLuint yuv_vao;
  GLuint yuv_vbo;
  GLuint yuv_ebo;
  glGenVertexArrays(1, &yuv_vao);
  glGenBuffers(1, &yuv_vbo);
  glGenBuffers(1, &yuv_ebo);
  glBindVertexArray(yuv_vao);
  glBindBuffer(GL_ARRAY_BUFFER, yuv_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(yuv_vertices), yuv_vertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, yuv_ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(yuv_indices), yuv_indices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);
  // Main loop
  AVPacket* av_packet = av_packet_alloc();
  AVFrame* av_frame = av_frame_alloc();
  const double time_base = static_cast<double>(format_context->streams[*video_stream]->time_base.num) / static_cast<double>(format_context->streams[*video_stream]->time_base.den) * 1000.0;
  const std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
  while (!glfwWindowShouldClose(window))
  {
    // Decode and build some frames if needed
    const uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
    while (frames.empty() || (current_time > frames.back().time_))
    {
      int ret = av_read_frame(format_context, av_packet);
      if (ret && (ret != AVERROR_EOF))
      {
        break;
      }
      else
      {
        if (av_packet->stream_index != *video_stream)
        {
          continue;
        }
        // Send packets
        if (avcodec_send_packet(codec_context, av_packet))
        {
          break;
        }
      }
      // Collect frames
      while (true)
      {
        if (avcodec_receive_frame(codec_context, av_frame))
        {
          break;
        }
        // Find a free frame
        std::optional<FRAME> frame;
        if (free_frames.empty())
        {
          // Clear up any old frames we can to make some space
          std::vector<FRAME>::iterator current_frame = frames.end();
          for (std::vector<FRAME>::iterator frame = frames.begin(); frame != frames.end(); ++frame)
          {
            if (current_time < frame->time_)
            {
              break;
            }
            current_frame = frame;
          }
          if (current_frame == frames.end())
          {
            // We've failed to find any frames, lets just give up
            return -1;
          }
          std::for_each(frames.begin(), current_frame, [&free_frames](const FRAME& frame) { free_frames.push_back(frame); });
          frames.erase(frames.begin(), current_frame);
          // Grab one
          frame = free_frames.back();
          free_frames.pop_back();
        }
        else
        {
          frame = free_frames.back();
          free_frames.pop_back();
        }
        frame->time_ = static_cast<uint64_t>(static_cast<double>(av_frame->pts) * time_base);
        frames.push_back(*frame);
        // Update textures with AVFrame data
        glBindFramebuffer(GL_FRAMEBUFFER, frame->framebuffer_);
        glUseProgram(yuv_shader_program);
        // Bind textures
        glBindTexture(GL_TEXTURE_2D, yuv_textures[0]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, av_frame->data[0]);
        glBindTexture(GL_TEXTURE_2D, yuv_textures[1]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width / 2, height / 2, GL_RED, GL_UNSIGNED_BYTE, av_frame->data[1]);
        glBindTexture(GL_TEXTURE_2D, yuv_textures[2]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width / 2, height / 2, GL_RED, GL_UNSIGNED_BYTE, av_frame->data[2]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, yuv_textures[0]);
        glUniform1i(glGetUniformLocation(yuv_shader_program, "texture_y"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, yuv_textures[1]);
        glUniform1i(glGetUniformLocation(yuv_shader_program, "texture_u"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, yuv_textures[2]);
        glUniform1i(glGetUniformLocation(yuv_shader_program, "texture_v"), 2);
        // Draw
        glBindVertexArray(yuv_vao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        // Clean up
        glUseProgram(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
      }
    }
    // Find the correct frame to display
    std::vector<FRAME>::iterator current_frame = frames.end();
    for (std::vector<FRAME>::iterator frame = frames.begin(); frame != frames.end(); ++frame)
    {
      if (current_time < frame->time_)
      {
        break;
      }
      current_frame = frame;
    }
    if (current_frame == frames.end())
    {
      return -1;
    }
    // ImGui stuff
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    // Window
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::NewFrame();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Frame", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::Image(reinterpret_cast<ImTextureID>(current_frame->texture_), ImVec2(width, height));
    ImGui::End();
    ImGui::EndFrame();
    ImGui::PopStyleVar(4);
    // Rendering
    ImGui::Render();
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }
  // Cleanup
  glDeleteShader(yuv_shader_program);
  glDeleteTextures(yuv_textures.size(), yuv_textures.data());
  glDeleteVertexArrays(1, &yuv_vao);
  glDeleteBuffers(1, &yuv_vbo);
  glDeleteBuffers(1, &yuv_ebo);
  for (const FRAME& frame : frames)
  {
    glDeleteFramebuffers(1, &frame.framebuffer_);
    glDeleteTextures(1, &frame.texture_);
  }
  for (const FRAME& frame : free_frames)
  {
    glDeleteFramebuffers(1, &frame.framebuffer_);
    glDeleteTextures(1, &frame.texture_);
  }
  // Codec
  avformat_free_context(format_context);
  avcodec_free_context(&codec_context);
  av_packet_free(&av_packet);
  av_frame_free(&av_frame);
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}