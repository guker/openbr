#include "iarpa_janus.h"
#include "iarpa_janus_io.h"
#include "openbr_plugin.h"
#include "openbr/core/opencvutils.h"
#include "openbr/core/common.h"

using namespace br;

static QSharedPointer<Transform> transform;
static QSharedPointer<Distance> distance;

size_t janus_max_template_size()
{
    return 33554432; // 32 MB
}

janus_error janus_initialize(const char *sdk_path, const char *model_file)
{
    int argc = 1;
    const char *argv[1] = { "janus" };
    Context::initialize(argc, (char**)argv, sdk_path, false);
    Globals->quiet = true;
    const QString algorithm = model_file;
    if (algorithm.isEmpty()) {
        transform.reset(Transform::make("Cvt(Gray)+Affine(88,88,0.25,0.35)+<FaceRecognitionExtraction>+<FaceRecognitionEmbedding>+<FaceRecognitionQuantization>", NULL));
        distance = Distance::fromAlgorithm("FaceRecognition");
    } else {
        transform.reset(Transform::make(algorithm + "Enroll", NULL));
        distance.reset(Distance::make(algorithm + "Compare", NULL));
    }
    return JANUS_SUCCESS;
}

janus_error janus_finalize()
{
    transform.reset();
    distance.reset();
    Context::finalize();
    return JANUS_SUCCESS;
}

struct janus_template_type : public Template
{};

janus_error janus_allocate(janus_template *template_)
{
    *template_ = new janus_template_type();
    return JANUS_SUCCESS;
}

janus_error janus_augment(const janus_image image, const janus_attribute_list attributes, janus_template template_)
{
    Template t;
    t.append(cv::Mat(image.height,
                     image.width,
                     image.color_space == JANUS_GRAY8 ? CV_8UC1 : CV_8UC3,
                     image.data));
    for (size_t i=0; i<attributes.size; i++)
        t.file.set(janus_attribute_to_string(attributes.attributes[i]), attributes.values[i]);

    if (!t.file.contains("RIGHT_EYE_X") ||
        !t.file.contains("RIGHT_EYE_Y") ||
        !t.file.contains("LEFT_EYE_X") ||
        !t.file.contains("LEFT_EYE_Y"))
        return JANUS_MISSING_ATTRIBUTES;

    t.file.set("Affine_0", QPointF(t.file.get<float>("RIGHT_EYE_X"), t.file.get<float>("RIGHT_EYE_Y")));
    t.file.set("Affine_1", QPointF(t.file.get<float>("LEFT_EYE_X"), t.file.get<float>("LEFT_EYE_Y")));
    t.file.appendPoint(t.file.get<QPointF>("Affine_1"));
    t.file.appendPoint(t.file.get<QPointF>("Affine_0"));
    Template u;
    transform->project(t, u);
    template_->append(u);
    return (u.isEmpty() || !u.first().data) ? JANUS_FAILURE_TO_ENROLL : JANUS_SUCCESS;
}

janus_error janus_flatten_template(janus_template template_, janus_flat_template flat_template, size_t *bytes)
{    
    *bytes = 0;
    foreach (const cv::Mat &m, *template_) {
        if (!m.data)
            continue;

        if (!m.isContinuous())
            return JANUS_UNKNOWN_ERROR;

        const size_t templateBytes = m.rows * m.cols * m.elemSize();
        if (*bytes + sizeof(size_t) + templateBytes > janus_max_template_size())
            break;

        memcpy(flat_template, &templateBytes, sizeof(templateBytes));
        flat_template += sizeof(templateBytes);
        *bytes += sizeof(templateBytes);

        memcpy(flat_template, m.data, templateBytes);
        flat_template += templateBytes;
        *bytes += templateBytes;
    }
    return JANUS_SUCCESS;
}

janus_error janus_flatten_gallery(janus_gallery gallery, janus_flat_gallery flat_gallery, size_t *bytes)
{
    *bytes = 0;
    foreach (const Template &t, TemplateList::fromGallery(gallery)) {
        janus_template_id template_id = t.file.get<janus_template_id>("TEMPLATE_ID");
        janus_flat_template u = new janus_data[janus_max_template_size()];
        size_t t_bytes = 0;
        foreach (const cv::Mat &m, t) {
            if (!m.data)
                continue;

            if (!m.isContinuous())
                return JANUS_UNKNOWN_ERROR;

            const size_t templateBytes = m.rows * m.cols * m.elemSize();
            if (*bytes + sizeof(size_t) + templateBytes > janus_max_template_size())
                break;

            memcpy(u, &templateBytes, sizeof(templateBytes));
            u += sizeof(templateBytes);
            t_bytes += sizeof(templateBytes);

            memcpy(u, m.data, templateBytes);
            u += templateBytes;
            t_bytes += templateBytes;
        }
        memcpy(flat_gallery, &template_id, sizeof(template_id));
        memcpy(flat_gallery, &t_bytes, sizeof(t_bytes));
        flat_gallery += sizeof(t_bytes);
        *bytes += sizeof(t_bytes);

        memcpy(flat_gallery, u, t_bytes);
        flat_gallery += t_bytes;
        *bytes += t_bytes;
    }
    return JANUS_SUCCESS;
}

janus_error janus_free(janus_template template_)
{
    delete template_;
    return JANUS_SUCCESS;
}

janus_error janus_verify(const janus_flat_template a, const size_t a_bytes, const janus_flat_template b, const size_t b_bytes, float *similarity)
{
    *similarity = 0;

    int comparisons = 0;
    janus_flat_template a_template = a;
    while (a_template < a + a_bytes) {
        const size_t a_template_bytes = *reinterpret_cast<size_t*>(a_template);
        a_template += sizeof(a_template_bytes);

        janus_flat_template b_template = b;
        while (b_template < b + b_bytes) {
                const size_t b_template_bytes = *reinterpret_cast<size_t*>(b_template);
                b_template += sizeof(b_template_bytes);

                *similarity += distance->compare(cv::Mat(1, a_template_bytes, CV_8UC1, a_template),
                                                 cv::Mat(1, b_template_bytes, CV_8UC1, b_template));
                comparisons++;

                b_template += b_template_bytes;
        }

        a_template += a_template_bytes;
    }

    if (*similarity != *similarity) // True for NaN
        return JANUS_UNKNOWN_ERROR;

    if (comparisons > 0) *similarity /= comparisons;
    else                 *similarity = -std::numeric_limits<float>::max();
    return JANUS_SUCCESS;
}

janus_error janus_enroll(const janus_template template_, const janus_template_id template_id, janus_gallery gallery)
{
    template_->file.set("TEMPLATE_ID", template_id);
    QFile file(gallery);
    if (!file.open(QFile::WriteOnly | QFile::Append))
        return JANUS_WRITE_ERROR;
    QDataStream stream(&file);
    stream << *template_;
    file.close();
    return JANUS_SUCCESS;
}

janus_error janus_gallery_size(janus_gallery gallery, size_t *size)
{
    *size = TemplateList::fromGallery(gallery).size();
    return JANUS_SUCCESS;
}

janus_error janus_search(const janus_template template_, janus_flat_gallery gallery, size_t gallery_bytes, int requested_returns, janus_template_id *template_ids, float *similarities, int *actual_returns)
{
    janus_flat_template query = new janus_data[janus_max_template_size()];
    size_t query_size;
    JANUS_ASSERT(janus_flatten_template(template_, query, &query_size))

    typedef QPair<float, int> Pair;
    QList<Pair> comparisons; comparisons.reserve(requested_returns);
    janus_flat_gallery target_gallery = gallery;
    while (target_gallery < gallery + gallery_bytes) {
        janus_template_id target_id = *reinterpret_cast<janus_template_id*>(target_gallery);
        target_gallery += sizeof(janus_template_id);

        const size_t target_gallery_bytes = *reinterpret_cast<size_t*>(target_gallery);
        target_gallery += sizeof(target_gallery_bytes);
        janus_flat_template target = new janus_data[janus_max_template_size()];
        memcpy(target, &target_gallery_bytes, sizeof(target_gallery_bytes));
        memcpy(target, &target_gallery, target_gallery_bytes);

        float similarity;
        JANUS_ASSERT(janus_verify(query, query_size, target, target_gallery_bytes, &similarity))
        if (comparisons.size() < requested_returns){
            comparisons.append(Pair(similarity, target_id));
            std::sort(comparisons.begin(), comparisons.end());
        } else {
            comparisons.removeLast();
            comparisons.append(Pair(similarity, target_id));
            std::sort(comparisons.begin(), comparisons.end());
        }

    }

    if (comparisons.size() < requested_returns) *actual_returns = comparisons.size();
    else                                    *actual_returns = requested_returns;

    foreach(const Pair &comparison, comparisons) {
        memcpy(similarities, &comparison.first, sizeof(comparison.first));
        memcpy(template_ids, &comparison.second, sizeof(comparison.second));
    }
    return JANUS_SUCCESS;
}

janus_error janus_compare(janus_flat_gallery target, size_t target_bytes, janus_flat_gallery query, size_t query_bytes, float *similarity_matrix, janus_template_id *target_ids, janus_template_id *query_ids)
{
    const TemplateList targets = TemplateList::fromGallery(target);
    const TemplateList queries = TemplateList::fromGallery(query);
    QScopedPointer<MatrixOutput> matrix(MatrixOutput::make(targets.files(), queries.files()));
    distance->compare(targets, queries, matrix.data());
    const QVector<janus_template_id> targetIds = File::get<janus_template_id,File>(matrix->targetFiles, "TEMPLATE_ID").toVector();
    const QVector<janus_template_id> queryIds  = File::get<janus_template_id,File>(matrix->queryFiles,  "TEMPLATE_ID").toVector();
    memcpy(similarity_matrix, matrix->data.data, matrix->data.rows * matrix->data.cols * sizeof(float));
    memcpy(target_ids, targetIds.data(), targetIds.size() * sizeof(janus_template_id));
    memcpy(query_ids, queryIds.data(), queryIds.size() * sizeof(janus_template_id));
    return JANUS_SUCCESS;
}
