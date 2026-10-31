#include "glorpviewer.h"

#include <array>
#include <algorithm>
#include <optional>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QFile>

// Note: Qt recommends a particular pattern for initializing and destroying QOpenGLWidgets
// https://doc.qt.io/qt-6/qopenglwidget.html

struct OrbInstance {
    float x;
    float z;
    float y;
    float d;
    int colourIdx;
};

enum PlotSymbol {
    // Orb plots, as seen in the game
    UnaryMinus = -1,
    Sep = -3,
    Radix = -10,
    Begin = -14,
    End = -15,
    Orb = -52,
    Plot = -53,
};

GlOrPViewer::GlOrPViewer(QWidget *parent) : QOpenGLWidget{parent} {
}

GlOrPViewer::~GlOrPViewer() {
    this->cleanup();
}

static float asDecimalFraction(qint64 nonNegativeInt) {
    // Known issue: It's not possible to represent decimals with fractional components that start with 0,
    // due to how DSCR clients canonically pack numbers
    float scaleFactor = 1.0f;
    while(scaleFactor <= float(nonNegativeInt)) {
        scaleFactor *= 10.0f;
    }
    return float(nonNegativeInt) / scaleFactor;
}

static std::optional<float> consumeUnsignedFloat(QSpan<const qint64> symbols, qsizetype &cur) {
    qsizetype cur2 = cur;
    if(cur2 >= symbols.size()) {
        return std::nullopt;
    }

    if(symbols[cur2] == PlotSymbol::Radix) {
        // Radix
        cur2++;
        if(cur2 >= symbols.size()) {
            return std::nullopt;
        }
        // Radix NONNEGATIVE_INTEGER
        if(symbols[cur2] >= 0) {
            float accumulator = asDecimalFraction(symbols[cur2]);
            cur2++;
            cur = cur2;
            return accumulator;
        } else {
            return std::nullopt;
        }
    } else if(symbols[cur2] >= 0) {
        // NONNEGATIVE_INTEGER
        float accumulator = symbols[cur2];
        cur2++;
        if(cur2 >= symbols.size()) {
            cur = cur2;
            return accumulator;
        }
        // NONNEGATIVE_INTEGER Radix
        if(symbols[cur2] == PlotSymbol::Radix) {
            cur2++;
            if(cur2 >= symbols.size()) {
                cur = cur2;
                return accumulator;
            }
            if(symbols[cur2] >= 0) {
                // NONNEGATIVE_INTEGER Radix NONNEGATIVE_INTEGER
                accumulator += asDecimalFraction(symbols[cur2]);
                cur2++;
                cur = cur2;
                return accumulator;
            } else {
                cur = cur2;
                return accumulator;
            }
        } else {
            cur = cur2;
            return accumulator;
        }
    } else {
        return std::nullopt;
    }
}

static std::optional<float> consumeSignedFloat(QSpan<const qint64> symbols, qsizetype &cur) {
    qsizetype cur2 = cur;
    if(cur2 >= symbols.size()) {
        return std::nullopt;
    }

    bool negate = false;
    if(symbols[cur2] == PlotSymbol::UnaryMinus) {
        // UnaryMinus
        negate = true;
        cur2++;
        if(cur2 >= symbols.size()) {
            return std::nullopt;
        }
    }

    auto maybeUnsignedNumber = consumeUnsignedFloat(symbols, cur2);
    if(maybeUnsignedNumber.has_value()) {
        cur = cur2;
        return negate ? -*maybeUnsignedNumber : *maybeUnsignedNumber;
    }
    return std::nullopt;
}

static bool consumeSep(QSpan<const qint64> &symbols, qsizetype &cur) {
    if(cur >= symbols.size() || symbols[cur] != PlotSymbol::Sep) {
        return false;
    }

    cur++;
    return true;
}

static std::optional<OrbInstance> consumeOrb(QSpan<const qint64> symbols, qsizetype &cur) {
    qsizetype cur2 = cur;
    if(cur2 >= symbols.size() || symbols[cur2] != PlotSymbol::Orb) {
        return std::nullopt;
    }
    cur2++;

    OrbInstance orb;
    auto maybeX = consumeSignedFloat(symbols, cur2);
    if(maybeX.has_value() && consumeSep(symbols, cur2)) {
        orb.x = *maybeX;
    } else {
        return std::nullopt;
    }
    auto maybeZ = consumeSignedFloat(symbols, cur2);
    if(maybeZ.has_value() && consumeSep(symbols, cur2)) {
        orb.z = -*maybeZ;
    } else {
        return std::nullopt;
    }
    auto maybeY = consumeSignedFloat(symbols, cur2);
    if(maybeY.has_value() && consumeSep(symbols, cur2)) {
        orb.y = *maybeY;
    } else {
        return std::nullopt;
    }
    auto maybeR = consumeUnsignedFloat(symbols, cur2);
    if(maybeR.has_value() && consumeSep(symbols, cur2)) {
        orb.d = *maybeR;
    } else {
        return std::nullopt;
    }
    if(cur2 < symbols.size() && symbols[cur2] >= 0) {
        orb.colourIdx = symbols[cur2];
        cur2++;
    } else {
        return std::nullopt;
    }

    cur = cur2;
    return orb;
}

static GLubyte lerp(GLubyte start, GLubyte end, float mix) {
    return std::lround(float(start) * (1.0f - mix) + float(end) * mix);
}

// Based off DSVE's colour table
// https://github.com/akqqa/deep-space-visual-editor/blob/69f12bc6033c01d975981e40cbeec890feeee853/src/js/editor.js#L46-L65
static std::array<GLubyte, 3> getIndexedColour(int index) {
    const GLubyte R[] = { 0xFF, 0xBB, 0x00, 0x00, 0x4D, 0xFB, 0xFF, 0x48, 0x63, 0xFF };
    const GLubyte G[] = { 0x58, 0xFF, 0xCD, 0x84, 0x00, 0x39, 0x0F, 0x48, 0x63, 0xFF };
    const GLubyte B[] = { 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xD7, 0x48, 0x63, 0xFF };

    float scaledIdx = float(std::clamp(index, 0, 64)) / 64.0f * (sizeof(R) - 1);
    int loIdx = std::floor(scaledIdx);
    int hiIdx = std::ceil(scaledIdx);
    float mix = scaledIdx - loIdx;

    GLubyte r = lerp(R[loIdx], R[hiIdx], mix);
    GLubyte g = lerp(G[loIdx], G[hiIdx], mix);
    GLubyte b = lerp(B[loIdx], B[hiIdx], mix);

    return { r, g, b };
}

bool GlOrPViewer::load53(QSpan<const qint64> symbols) {
    // PLOT := Plot Begin End
    //       | Plot Begin ORB (Sep ORB)* Sep? End
    // ORB := Orb SNUMBER Sep SNUMBER Sep SNUMBER Sep UNUMBER Sep NONNEGATIVE_INTEGER
    // UNUMBER := NONNEGATIVE_INTEGER Radix?
    //          | Radix NONNEGATIVE_INTEGER
    //          | NONNEGATIVE_INTEGER Radix NONNEGATIVE_INTEGER
    // SNUMBER := UnaryMinus? UNUMBER

    if(symbols.size() < 3) {
        return false;
    }
    if(symbols[0] != PlotSymbol::Plot || symbols[1] != PlotSymbol::Begin) {
        return false;
    }
    QVector<OrbInstance> orbs;
    qsizetype cur = 2;
    if(auto maybeOrb = consumeOrb(symbols, cur); maybeOrb.has_value()) {
        orbs.append(*maybeOrb);
        while(cur < symbols.size() && symbols[cur] == PlotSymbol::Sep) {
            cur++;
            if(cur < symbols.size() && symbols[cur] == PlotSymbol::End) {
                // allow trailing Sep (C/Python-like semantics)
                break;
            }
            auto maybeOrb2 = consumeOrb(symbols, cur);
            if(!maybeOrb2.has_value()) {
                return false;
            }
            orbs.append(*maybeOrb2);
        }
    }
    if(cur != symbols.size() - 1 || symbols[cur] != PlotSymbol::End) {
        return false;
    }

    this->m_drawInstances.clear();
    for(auto &orb : orbs) {
        QMatrix4x4 model;
        DrawInstance oai;

        model.translate(orb.x, orb.y, orb.z);
        model.scale(orb.d / 2.0f);
        std::copy_n(model.constData(), 16, oai.m);
        auto colour = getIndexedColour(orb.colourIdx);
        oai.rgba[0] = colour[0];
        oai.rgba[1] = colour[1];
        oai.rgba[2] = colour[2];
        oai.rgba[3] = 255;

        this->m_drawInstances.append(oai);
    }
    this->m_drawInstancesDirty = true;

    // caller should call this->update() when ready to present
    return true;
}

void GlOrPViewer::initializeGL() {
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    QOpenGLExtraFunctions *g = QOpenGLContext::currentContext()->extraFunctions();

    f->glEnable(GL_DEPTH_TEST);
    f->glEnable(GL_CULL_FACE);

    m_program = new QOpenGLShaderProgram();
    m_program->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/blinn_phong_vertex.glsl");
    m_program->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/blinn_phong_frag.glsl");
    m_program->link();

    m_vao.create();
    if (m_vao.isCreated()) {
        m_vao.bind();
    }

    QVector<GLfloat> vertices;
    QVector<GLuint> indices;

    // Parser hardcoded specifically for reading a Blender-exported obj file with no normal or UV data
    QFile icoSphFile(":/meshes/icosphere4.obj");
    if(icoSphFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while(!icoSphFile.atEnd()) {
            auto line = icoSphFile.readLine().simplified().split(' ');
            if(line[0] == "v") {
                // stride = 24, offset = 0
                vertices.append(line[1].toFloat());
                vertices.append(line[2].toFloat());
                vertices.append(line[3].toFloat());
                // offset = 3
                // For the case of a unit icosphere, normal is equal to position
                vertices.append(line[1].toFloat());
                vertices.append(line[2].toFloat());
                vertices.append(line[3].toFloat());
            } else if(line[0] == "f") {
                indices.append(line[1].toInt() - 1);
                indices.append(line[2].toInt() - 1);
                indices.append(line[3].toInt() - 1);
            }
        }
    }

    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(vertices.data(), vertices.size() * sizeof(GLfloat));
    const int STRIDE = 6 * sizeof(GLfloat);
    m_program->enableAttributeArray(0);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 3, STRIDE);
    m_program->enableAttributeArray(1);
    m_program->setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(GLfloat), 3, STRIDE);
    m_vbo.release();

    this->m_ibo.create();
    this->m_ibo.bind();
    this->m_ibo.allocate(indices.data(), indices.size() * sizeof(GLuint));
    this->m_numIboIndices = indices.size();

    // QVector<DrawInstance> orbInstances;

    // QMatrix4x4 model;
    // DrawInstance oai;
    // model.translate(QVector3D(0.1, 0.2, 0.3));
    // model.scale(0.1);
    // std::copy_n(model.constData(), 16, oai.m);
    // oai.rgba[0] = 0;
    // oai.rgba[1] = 255;
    // oai.rgba[2] = 0;
    // oai.rgba[3] = 255;
    // orbInstances.append(oai);

    // model.setToIdentity();
    // model.translate(QVector3D(0.3, 0.4, 0.5));
    // model.scale(0.3);
    // std::copy_n(model.constData(), 16, oai.m);
    // oai.rgba[0] = 0;
    // oai.rgba[1] = 0;
    // oai.rgba[2] = 255;
    // oai.rgba[3] = 255;
    // orbInstances.append(oai);

    this->m_ivbo.create();
    this->m_ivbo.bind();
    // this->m_ivbo.allocate(orbInstances.data(), orbInstances.size() * sizeof(DrawInstance));
    this->m_numArrayInstances = 0; //orbInstances.size();
    for(int i = 0; i < 4; i++) { // 2-5
        m_program->enableAttributeArray(2 + i);
        m_program->setAttributeBuffer(2 + i, GL_FLOAT, i * 4 * sizeof(GLfloat), 4, sizeof(DrawInstance));
        g->glVertexAttribDivisor(2 + i, 1);
    }
    m_program->enableAttributeArray(6);
    m_program->setAttributeBuffer(6, GL_UNSIGNED_BYTE, 4 * 4 * sizeof(GLfloat), 4, sizeof(DrawInstance));
    g->glVertexAttribDivisor(6, 1);
    this->m_ivbo.release();

    m_vao.release();

    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &GlOrPViewer::cleanup);
}

void GlOrPViewer::resizeGL(int w, int h) {
    m_projection.setToIdentity();
    m_projection.perspective(45.0f, w / float(h), 0.01f, 100.0f);
}

void GlOrPViewer::paintGL() {
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    QOpenGLExtraFunctions *g = QOpenGLContext::currentContext()->extraFunctions();
    f->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    QVector3D cameraPos(0.0f, 0.0f, 50.0f);
    QVector3D targetPos(0.0f, 0.0f, 0.0f);
    QVector3D cameraUp(0.0f, 1.0f, 0.0f);
    QMatrix4x4 view;
    view.lookAt(cameraPos, targetPos, cameraUp);

    this->m_program->bind();
    this->m_program->setUniformValue("vp", m_projection * view);
    this->m_program->setUniformValue("viewPos", cameraPos);
    this->m_program->setUniformValue("lightPos", cameraPos + QVector3D(-2.0f, 2.0f, 0.0f));

    if(this->m_drawInstancesDirty) {
        this->m_ivbo.bind();
        this->m_ivbo.allocate(this->m_drawInstances.data(), this->m_drawInstances.size() * sizeof(DrawInstance));
        this->m_ivbo.release();
        this->m_numArrayInstances = this->m_drawInstances.size();
        this->m_drawInstancesDirty = false;
    }

    this->m_vao.bind();
    g->glDrawElementsInstanced(GL_TRIANGLES, this->m_numIboIndices, GL_UNSIGNED_INT, nullptr, this->m_numArrayInstances);
    this->m_vao.release();
    this->m_program->release();
}

void GlOrPViewer::cleanup() {
    this->makeCurrent();

    delete this->m_program;
    this->m_program = nullptr;

    this->m_ivbo.destroy();
    this->m_ibo.destroy();
    this->m_vbo.destroy();
    this->m_vao.destroy();

    this->doneCurrent();
    disconnect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &GlOrPViewer::cleanup);
}
