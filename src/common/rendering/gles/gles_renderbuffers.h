
#pragma once

#include "gles_system.h"
#include "hwrenderer/postprocessing/hw_postprocess.h"

namespace OpenGLESRenderer {

class FGLRenderBuffers;

class PPGLTexture {
public:
  void Bind(int index, int filter = GL_NEAREST, int wrap = GL_CLAMP_TO_EDGE) {
    glActiveTexture(GL_TEXTURE0 + index);
    glBindTexture(GL_TEXTURE_2D, handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
  }

  int Width = -1;
  int Height = -1;

  explicit operator bool() const { return handle != 0; }

private:
  GLuint handle = 0;

  friend class FGLRenderBuffers;
  friend class PPGLTextureBackend;
};

class PPGLFrameBuffer {
public:
  void Bind() { glBindFramebuffer(GL_FRAMEBUFFER, handle); }

  explicit operator bool() const { return handle != 0; }

private:
  GLuint handle = 0;

  friend class FGLRenderBuffers;
  friend class PPGLTextureBackend;
};

class PPGLRenderBuffer {
private:
  GLuint handle = 0;

  explicit operator bool() const { return handle != 0; }

  friend class FGLRenderBuffers;
};

class PPGLTextureBackend : public PPTextureBackend {
public:
  ~PPGLTextureBackend() {
    if (Tex.handle != 0) {
      glDeleteTextures(1, &Tex.handle);
      Tex.handle = 0;
    }
    if (FB.handle != 0) {
      glDeleteFramebuffers(1, &FB.handle);
      FB.handle = 0;
    }
  }

  PPGLTexture Tex;
  PPGLFrameBuffer FB;
};

class FShaderProgram;

class GLPPRenderState : public PPRenderState {
public:
  GLPPRenderState(FGLRenderBuffers *buffers) : buffers(buffers) { }

  void PushGroup(const FString &name) override;
  void PopGroup() override;
  void Draw() override;

private:
  PPGLTextureBackend *GetGLTexture(PPTexture *texture);
  FShaderProgram *GetGLShader(PPShader *shader);

  FGLRenderBuffers *buffers;
};

class FGLRenderBuffers {
public:
  FGLRenderBuffers();
  ~FGLRenderBuffers();

  void Setup(int width, int height, int sceneWidth, int sceneHeight);

  void BindSceneFB(bool sceneData);
  void BindSceneColorTexture(int index, int filter = GL_NEAREST,
                            int wrap = GL_CLAMP_TO_EDGE);
  void BindSceneFogTexture(int index, int filter = GL_NEAREST,
                          int wrap = GL_CLAMP_TO_EDGE);
  void BindSceneNormalTexture(int index, int filter = GL_NEAREST,
                             int wrap = GL_CLAMP_TO_EDGE);
  void BindSceneDepthTexture(int index, int filter = GL_NEAREST,
                            int wrap = GL_CLAMP_TO_EDGE);

  void BindCurrentTexture(int index, int filter = GL_NEAREST,
                          int wrap = GL_CLAMP_TO_EDGE);
  void BindCurrentFB();
  void BindNextFB();
  void NextTexture();

  void BindOutputFB();

  void BindDitherTexture(int texunit);

  int GetWidth() const { return mWidth; }
  int GetHeight() const { return mHeight; }

  int GetSceneWidth() const { return mSceneWidth; }
  int GetSceneHeight() const { return mSceneHeight; }

private:
  void ClearScene();

  void CreateScene(int width, int height);
  void CreatePipeline(int width, int height);

  PPGLTexture Create2DTexture(const char *name, GLuint format, int width,
                              int height, const void *data = nullptr);
  PPGLRenderBuffer CreateRenderBuffer(const char *name, GLuint format,
                                      int width, int height);
  PPGLFrameBuffer CreateFrameBuffer(const char *name, PPGLTexture colorbuffer);
  PPGLFrameBuffer CreateFrameBuffer(const char *name, PPGLTexture colorbuffer,
                                    PPGLRenderBuffer depthstencil,
                                    PPGLRenderBuffer stencil);
  PPGLFrameBuffer CreateFrameBuffer(const char *name,
                                    PPGLRenderBuffer colorbuffer,
                                    PPGLRenderBuffer depthstencil,
                                    PPGLRenderBuffer stencil);
  PPGLFrameBuffer CreateFrameBuffer(const char *name, PPGLTexture colorbuffer0,
                                    PPGLTexture colorbuffer1,
                                    PPGLTexture colorbuffer2,
                                    PPGLTexture depthstencil,
                                    PPGLRenderBuffer stencil);
  bool CheckFrameBufferCompleteness();
  void ClearFrameBuffer(bool stencil, bool depth);
  void DeleteTexture(PPGLTexture &handle);
  void DeleteRenderBuffer(PPGLRenderBuffer &handle);
  void DeleteFrameBuffer(PPGLFrameBuffer &handle);

  void ClearPipeline();

  int mWidth = 0;
  int mHeight = 0;
  int mSceneWidth = 0;
  int mSceneHeight = 0;
  static const int NumPipelineTextures = 2;
  int mCurrentPipelineTexture = 0;

  // Buffers for the scene
  PPGLRenderBuffer mSceneDepthStencilBuf;
  PPGLRenderBuffer mSceneStencilBuf; // This is only use when combined
                                     // depth-stencil is not avaliable
  PPGLFrameBuffer mSceneFB;

  // Effect buffers
  PPGLTexture mPipelineTexture[NumPipelineTextures];
  PPGLFrameBuffer mPipelineFB[NumPipelineTextures];
  PPGLRenderBuffer mPipelineDepthStencilBuf;

  PPGLTexture mDitherTexture;

  static bool FailedCreate;

  friend class GLPPRenderState;
};

} // namespace OpenGLESRenderer
