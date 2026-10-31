#pragma once

#include <QOpenGLWidget>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLShader>
#include <QMatrix4x4>

class OGlOrbPlotViewer : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit OGlOrbPlotViewer(QWidget *parent = nullptr);
    ~OGlOrbPlotViewer();

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
    int m_numIboIndices = 0;
    QOpenGLShaderProgram *m_program = nullptr;

    QMatrix4x4 m_projection;
};
