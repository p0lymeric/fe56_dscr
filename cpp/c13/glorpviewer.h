#pragma once
#include "dscrtypes.h"

#include <QOpenGLWidget>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLShader>
#include <QMatrix4x4>

struct DrawInstance {
    GLfloat m[16];
    GLubyte rgba[4];
};

class GlOrPViewer : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit GlOrPViewer(QWidget *parent = nullptr);
    ~GlOrPViewer();

    bool load53(const DscrSpecialPlot &symbols);

signals:

protected:
    virtual void initializeGL() override;
    virtual void resizeGL(int w, int h) override;
    virtual void paintGL() override;

private:
    void cleanup();

    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer m_ibo{QOpenGLBuffer::IndexBuffer};
    QOpenGLBuffer m_ivbo{QOpenGLBuffer::VertexBuffer};
    int m_numIboIndices = 0;
    int m_numArrayInstances = 0;
    QOpenGLShaderProgram *m_program = nullptr;

    QVector<DrawInstance> m_drawInstances;
    bool m_drawInstancesDirty = false;;
    float m_boundingSphereRadius = 0.0f;
    float m_currentDrawboxAspectRatio = 1.0f;
};
