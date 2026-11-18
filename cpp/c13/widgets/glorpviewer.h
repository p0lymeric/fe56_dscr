#pragma once
#include "protocol/dscrtypes.h"

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

struct MeshInstanceCollection {
    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer ibo{QOpenGLBuffer::IndexBuffer};
    QOpenGLBuffer ivbo{QOpenGLBuffer::VertexBuffer};
    int numIboIndices = 0;
    int numArrayInstances = 0;
    QVector<DrawInstance> drawInstances;
};

class GlOrPViewer : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit GlOrPViewer(QWidget *parent = nullptr);
    ~GlOrPViewer();

    bool load53(const DscrSpecialPlot &plot);

signals:

protected:
    virtual void initializeGL() override;
    virtual void resizeGL(int w, int h) override;
    virtual void paintGL() override;

private:
    void cleanup();

    MeshInstanceCollection m_spheres;
    MeshInstanceCollection m_cubes;
    QOpenGLShaderProgram *m_program = nullptr;

    bool m_drawInstancesDirty = false;;
    float m_boundingSphereRadius = 0.0f;
    float m_currentDrawboxAspectRatio = 1.0f;
};
