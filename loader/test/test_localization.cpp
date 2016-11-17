/*
 Copyright 2016 Nervana Systems Inc.
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#include <vector>
#include <string>
#include <sstream>
#include <random>

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "gtest/gtest.h"
#include "gen_image.hpp"
#include "json.hpp"

#define private public

#include "etl_image.hpp"
#include "etl_localization.hpp"
#include "json.hpp"
#include "provider_factory.hpp"

using namespace std;
using namespace nervana;
using namespace nervana::localization;

static vector<string> label_list = {"person", "dog",  "lion", "tiger",   "eel",       "puma",
                                    "rat",    "tick", "flea", "bicycle", "hovercraft"};

static string read_file(const string& path)
{
    ifstream     f(path);
    stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static image::config make_image_config(int width = 1000, int height = 1000)
{
    nlohmann::json ijs = {{"width", width},
                          {"height", height},
                          {"channels", 3},
                          {"flip_enable", false},
                          {"crop_enable", false},
                          {"fixed_aspect_ratio", true},
                          {"fixed_scaling_factor", 1.6}};
    return image::config{ijs};
}

static localization::config make_localization_config(const image::config& icfg)
{
    nlohmann::json js = {{"class_names", label_list}, {"max_gt_boxes", 64}};
    return localization::config(js, icfg);
}

vector<uint8_t> make_image_from_metadata(const std::string& metadata)
{
    nlohmann::json  js     = nlohmann::json::parse(metadata);
    int             width  = js["size"]["width"];
    int             height = js["size"]["height"];
    cv::Mat         test_image(height, width, CV_8UC3);
    vector<uint8_t> test_image_data;
    cv::imencode(".png", test_image, test_image_data);
    return test_image_data;
}

TEST(localization, generate_anchors)
{
    // Verify that we compute the same anchors as Shaoqing's matlab implementation:
    //    >> load output/rpn_cachedir/faster_rcnn_VOC2007_ZF_stage1_rpn/anchors.mat
    //    >> anchors
    //
    //    anchors =
    //       -83   -39   100    56
    //      -175   -87   192   104
    //      -359  -183   376   200
    //       -55   -55    72    72
    //      -119  -119   136   136
    //      -247  -247   264   264
    //       -35   -79    52    96
    //       -79  -167    96   184
    //      -167  -343   184   360
    //
    // base_size 16, ratios [0.5, 1, 2], scales [ 8 16 32]

    vector<box> expected = {
        {-83.0 - 1.0, -39.0 - 1.0, 100.0 - 1.0, 56.0 - 1.0},    {-175.0 - 1.0, -87.0 - 1.0, 192.0 - 1.0, 104.0 - 1.0},
        {-359.0 - 1.0, -183.0 - 1.0, 376.0 - 1.0, 200.0 - 1.0}, {-55.0 - 1.0, -55.0 - 1.0, 72.0 - 1.0, 72.0 - 1.0},
        {-119.0 - 1.0, -119.0 - 1.0, 136.0 - 1.0, 136.0 - 1.0}, {-247.0 - 1.0, -247.0 - 1.0, 264.0 - 1.0, 264.0 - 1.0},
        {-35.0 - 1.0, -79.0 - 1.0, 52.0 - 1.0, 96.0 - 1.0},     {-79.0 - 1.0, -167.0 - 1.0, 96.0 - 1.0, 184.0 - 1.0},
        {-167.0 - 1.0, -343.0 - 1.0, 184.0 - 1.0, 360.0 - 1.0}};

    // subtract 1 from the expected vector as it was generated with 1's based matlab
    //    expected -= 1;

    auto cfg = make_localization_config(make_image_config());

    vector<box> actual      = anchor::generate_anchors(cfg.base_size, cfg.ratios, cfg.scales);
    vector<box> all_anchors = anchor::generate(cfg);
    ASSERT_EQ(expected.size(), actual.size());
    EXPECT_EQ(34596, all_anchors.size());
    EXPECT_EQ((9 * (62 * 62)), all_anchors.size());
    for (int i = 0; i < expected.size(); i++)
    {
        ASSERT_EQ(expected[i], actual[i]);
    }
}

void plot(const vector<box>& list, const string& prefix)
{
    float xmin = 0.0;
    float xmax = 0.0;
    float ymin = 0.0;
    float ymax = 0.0;
    for (const box& b : list)
    {
        xmin = std::min(xmin, b.xmin);
        xmax = std::max(xmax, b.xmax);
        ymin = std::min(ymin, b.ymin);
        ymax = std::max(ymax, b.ymax);
    }

    cv::Mat img(ymax - ymin, xmax - xmin, CV_8UC3);
    img = cv::Scalar(255, 255, 255);

    for (box b : list)
    {
        b.xmin -= xmin;
        b.xmax -= xmin;
        b.ymin -= ymin;
        b.ymax -= ymin;
        cv::rectangle(img, b.rect(), cv::Scalar(255, 0, 0));
    }
    box b = list[0];
    b.xmin -= xmin;
    b.xmax -= xmin;
    b.ymin -= ymin;
    b.ymax -= ymin;

    cv::rectangle(img, b.rect(), cv::Scalar(0, 0, 255));

    string fname = to_string(int(list[0].width())) + "x" + to_string(int(list[0].height())) + ".png";
    fname        = prefix + fname;
    cv::imwrite(fname, img);
}

void plot(const string& path)
{
    string                    prefix = path.substr(path.size() - 11, 6) + "-";
    string                    data   = read_file(path);
    auto                      cfg    = make_localization_config(make_image_config());
    localization::extractor   extractor{cfg};
    localization::transformer transformer{cfg};
    auto                      extracted_metadata = extractor.extract(&data[0], data.size());
    ASSERT_NE(nullptr, extracted_metadata);
    auto                              params               = make_shared<image::params>();
    shared_ptr<localization::decoded> transformed_metadata = transformer.transform(params, extracted_metadata);

    const vector<box>& an = transformer.all_anchors;

    int         last_width  = 0;
    int         last_height = 0;
    vector<box> list;
    for (const box& b : an)
    {
        if (last_width != b.width() || last_height != b.height())
        {
            if (list.size() > 0)
            {
                plot(list, prefix);
                list.clear();
            }
        }
        list.push_back(b);
        last_width  = b.width();
        last_height = b.height();
    }
    if (list.size() > 0)
    {
        plot(list, prefix);
    }

    vector<int> labels       = transformed_metadata->labels;
    vector<int> anchor_index = transformed_metadata->anchor_index;

    //    for(int i=0; i<transformed_metadata->anchor_index.size(); i++) {
    //        cout << "loader " << i << " " << transformed_metadata->anchor_index[i] << " " <<
    //        labels[transformed_metadata->anchor_index[i]] << endl;
    //        cout << an[transformed_metadata->anchor_index[i]] << endl;
    //    }

    {
        cv::Mat img(extracted_metadata->output_image_size, CV_8UC3);
        img = cv::Scalar(255, 255, 255);
        // Draw foreground boxes
        for (int i = 0; i < anchor_index.size(); i++)
        {
            int index = anchor_index[i];
            if (labels[index] == 1)
            {
                box abox = an[index];
                cv::rectangle(img, abox.rect(), cv::Scalar(0, 255, 0));
            }
        }

        // Draw bounding boxes
        for (box b : extracted_metadata->boxes())
        {
            b = b * extracted_metadata->image_scale;
            cv::rectangle(img, b.rect(), cv::Scalar(255, 0, 0));
        }
        cv::imwrite(prefix + "fg.png", img);
    }

    {
        cv::Mat img(extracted_metadata->output_image_size, CV_8UC3);
        img = cv::Scalar(255, 255, 255);
        // Draw background boxes
        for (int i = 0; i < anchor_index.size(); i++)
        {
            int index = anchor_index[i];
            if (labels[index] == 0)
            {
                box abox = an[index];
                cv::rectangle(img, abox.rect(), cv::Scalar(0, 0, 255));
            }
        }

        // Draw bounding boxes
        for (box b : extracted_metadata->boxes())
        {
            b = b * extracted_metadata->image_scale;
            cv::rectangle(img, b.rect(), cv::Scalar(255, 0, 0));
        }
        cv::imwrite(prefix + "bg.png", img);
    }
}

// TEST(DISABLED_localization,plot) {
//     plot(CURDIR"/test_data/009952.json");
// }

TEST(localization, config)
{
    nlohmann::json ijs = {{"height", 300}, {"width", 400}, {"channels", 3}, {"flip_enable", false}};
    nlohmann::json js  = {{"class_names", label_list}, {"max_gt_boxes", 100}};
    image::config  icfg{ijs};

    EXPECT_NO_THROW(localization::config cfg(js, icfg));
}

TEST(localization, config_rotate)
{
    nlohmann::json ijs = {{"height", 300}, {"width", 400}, {"channels", 3}, {"flip_enable", false}, {"angle", {0, 90}}};
    nlohmann::json js  = {{"class_names", label_list}, {"max_gt_boxes", 100}};
    image::config  icfg{ijs};

    EXPECT_THROW(localization::config cfg(js, icfg), std::invalid_argument);
}

TEST(localization, sample_anchors)
{
    string                    data         = read_file(CURDIR "/test_data/006637.json");
    auto                      image_config = make_image_config();
    localization::config      cfg          = make_localization_config(image_config);
    localization::extractor   extractor{cfg};
    localization::transformer transformer{cfg};
    auto                      extracted_metadata = extractor.extract(&data[0], data.size());
    ASSERT_NE(nullptr, extracted_metadata);

    vector<unsigned char>      img = make_image_from_metadata(data);
    image::extractor           ext{image_config};
    shared_ptr<image::decoded> decoded = ext.extract((char*)&img[0], img.size());
    image::param_factory       factory(image_config);
    shared_ptr<image::params>  params = factory.make_params(decoded);

    auto transformed_metadata = transformer.transform(params, extracted_metadata);
    ASSERT_NE(nullptr, transformed_metadata);

    vector<int>    labels       = transformed_metadata->labels;
    vector<target> bbox_targets = transformed_metadata->bbox_targets;
    vector<int>    anchor_index = transformed_metadata->anchor_index;
    vector<box>    anchors      = transformer.all_anchors;

    EXPECT_EQ(34596, labels.size());
    EXPECT_EQ(34596, bbox_targets.size());
    EXPECT_EQ(256, anchor_index.size());
    EXPECT_EQ(34596, anchors.size());

    for (int index : anchor_index)
    {
        EXPECT_GE(index, 0);
        EXPECT_LT(index, 34596);
    }
    for (int index : anchor_index)
    {
        box b = anchors[index];
        EXPECT_GE(b.xmin, 0);
        EXPECT_GE(b.ymin, 0);
        EXPECT_LT(b.xmax, cfg.output_width);
        EXPECT_LT(b.ymax, cfg.output_height);
    }
}

TEST(localization, transform_scale)
{
    string          metadata   = read_file(CURDIR "/test_data/006637.json");
    vector<uint8_t> image_data = make_image_from_metadata(metadata);

    auto                       image_config = make_image_config(600, 600);
    auto                       cfg          = make_localization_config(image_config);
    image::param_factory       factory{image_config};
    image::extractor           image_extractor{image_config};
    shared_ptr<image::decoded> image_decoded = image_extractor.extract((const char*)image_data.data(), image_data.size());
    shared_ptr<image::params>  params        = factory.make_params(image_decoded);
    params->debug_deterministic              = true;

    localization::extractor   extractor{cfg};
    localization::transformer transformer{cfg};
    auto                      decoded_data = extractor.extract(&metadata[0], metadata.size());
    ASSERT_NE(nullptr, decoded_data);

    shared_ptr<localization::decoded> transformed_data = transformer.transform(params, decoded_data);

    for (int i = 0; i < decoded_data->boxes().size(); i++)
    {
        boundingbox::box expected = decoded_data->boxes()[i];
        boundingbox::box actual   = transformed_data->gt_boxes[i];
        expected.xmin *= transformed_data->image_scale;
        expected.ymin *= transformed_data->image_scale;
        expected.xmax *= transformed_data->image_scale;
        expected.ymax *= transformed_data->image_scale;
        EXPECT_EQ(expected.xmin, actual.xmin);
        EXPECT_EQ(expected.xmax, actual.xmax);
        EXPECT_EQ(expected.ymin, actual.ymin);
        EXPECT_EQ(expected.ymax, actual.ymax);
    }
}

TEST(localization, transform_flip)
{
    string          metadata   = read_file(CURDIR "/test_data/006637.json");
    vector<uint8_t> image_data = make_image_from_metadata(metadata);

    auto                       image_config = make_image_config(1000, 1000);
    auto                       cfg          = make_localization_config(image_config);
    image::param_factory       factory{image_config};
    image::extractor           image_extractor{image_config};
    shared_ptr<image::decoded> image_decoded = image_extractor.extract((const char*)image_data.data(), image_data.size());
    shared_ptr<image::params>  params        = factory.make_params(image_decoded);
    params->debug_deterministic              = true;
    params->flip                             = 1;

    localization::extractor   extractor{cfg};
    localization::transformer transformer{cfg};
    auto                      decoded_data = extractor.extract(&metadata[0], metadata.size());
    ASSERT_NE(nullptr, decoded_data);

    shared_ptr<localization::decoded> transformed_data = transformer.transform(params, decoded_data);

    for (int i = 0; i < decoded_data->boxes().size(); i++)
    {
        boundingbox::box expected = decoded_data->boxes()[i];
        boundingbox::box actual   = transformed_data->gt_boxes[i];

        // flip
        auto xmin        = expected.xmin;
        int  image_width = 500;
        expected.xmin    = image_width - expected.xmax - 1;
        expected.xmax    = image_width - xmin - 1;

        // scale
        float scale = 1.6;
        expected.xmin *= scale;
        expected.ymin *= scale;
        expected.xmax *= scale;
        expected.ymax *= scale;

        EXPECT_EQ(expected.xmin, actual.xmin);
        EXPECT_EQ(expected.xmax, actual.xmax);
        EXPECT_EQ(expected.ymin, actual.ymin);
        EXPECT_EQ(expected.ymax, actual.ymax);
    }
}

static boundingbox::box crop_single_box(boundingbox::box expected, cv::Rect cropbox, float scale)
{
    expected.xmin -= cropbox.x;
    expected.xmax -= cropbox.x;
    expected.ymin -= cropbox.y;
    expected.ymax -= cropbox.y;

    expected.xmin = max<float>(expected.xmin, 0);
    expected.ymin = max<float>(expected.ymin, 0);
    expected.xmax = max<float>(expected.xmax, 0);
    expected.ymax = max<float>(expected.ymax, 0);

    expected.xmin = min<float>(expected.xmin, cropbox.width);
    expected.ymin = min<float>(expected.ymin, cropbox.height);
    expected.xmax = min<float>(expected.xmax, cropbox.width);
    expected.ymax = min<float>(expected.ymax, cropbox.height);

    expected.xmin *= scale;
    expected.ymin *= scale;
    expected.xmax *= scale;
    expected.ymax *= scale;

    return expected;
}

bool is_box_valid(boundingbox::box b)
{
    return b.xmin != b.xmax && b.ymin != b.ymax;
}

TEST(localization, transform_crop)
{
    {
        string          data            = read_file(CURDIR "/test_data/006637.json");
        vector<uint8_t> test_image_data = make_image_from_metadata(data);

        nlohmann::json ijs          = {{"width", 600}, {"height", 600}, {"flip_enable", false}, {"scale", {0.8, 0.8}}};
        auto           image_config = image::config{ijs};

        auto                       cfg = make_localization_config(image_config);
        image::param_factory       factory{image_config};
        image::extractor           image_extractor{image_config};
        shared_ptr<image::decoded> image_decoded =
            image_extractor.extract((const char*)test_image_data.data(), test_image_data.size());
        shared_ptr<image::params> params = factory.make_params(image_decoded);

        localization::extractor   extractor{cfg};
        localization::transformer transformer{cfg};
        auto                      decoded_data = extractor.extract(&data[0], data.size());
        ASSERT_NE(nullptr, decoded_data);
        shared_ptr<localization::decoded> transformed_data = transformer.transform(params, decoded_data);

        EXPECT_EQ(6, transformed_data->gt_boxes.size());
        float scale = 2.0;
        for (int i = 0; i < transformed_data->gt_boxes.size(); i++)
        {
            boundingbox::box expected = decoded_data->boxes()[i];
            boundingbox::box actual   = transformed_data->gt_boxes[i];
            expected                  = crop_single_box(expected, params->cropbox, scale);
            EXPECT_EQ(expected, actual);
        }
    }
    {
        string          data            = read_file(CURDIR "/test_data/006637.json");
        vector<uint8_t> test_image_data = make_image_from_metadata(data);

        nlohmann::json ijs          = {{"width", 600}, {"height", 600}, {"flip_enable", false}, {"scale", {0.2, 0.2}}};
        auto           image_config = image::config{ijs};

        auto                       cfg = make_localization_config(image_config);
        image::param_factory       factory{image_config};
        image::extractor           image_extractor{image_config};
        shared_ptr<image::decoded> image_decoded =
            image_extractor.extract((const char*)test_image_data.data(), test_image_data.size());
        shared_ptr<image::params> params = factory.make_params(image_decoded);

        localization::extractor   extractor{cfg};
        localization::transformer transformer{cfg};
        auto                      decoded_data = extractor.extract(&data[0], data.size());
        ASSERT_NE(nullptr, decoded_data);
        shared_ptr<localization::decoded> transformed_data = transformer.transform(params, decoded_data);

        vector<boundingbox::box> valid_boxes;
        float                    scale = 8.0;
        for (auto b : decoded_data->boxes())
        {
            auto cropped = crop_single_box(b, params->cropbox, scale);
            if (is_box_valid(cropped))
            {
                valid_boxes.push_back(cropped);
            }
        }

        ASSERT_EQ(valid_boxes.size(), transformed_data->gt_boxes.size());
        for (int i = 0; i < transformed_data->gt_boxes.size(); i++)
        {
            boundingbox::box expected = valid_boxes[i];
            boundingbox::box actual   = transformed_data->gt_boxes[i];
            EXPECT_EQ(expected, actual);
        }
    }
}

TEST(localization, loader)
{
    vector<int> bbox_mask_index = {
        1200,   1262,   1324,   1386,   23954,  24016,  24078,  24090,  24140,  24152,  24202,  24214,  24264,  24276,
        24338,  24400,  24462,  24503,  24524,  24565,  24586,  24648,  27977,  27978,  28039,  28040,  28101,  28102,
        28163,  28164,  28225,  28226,  28287,  28559,  28560,  35796,  35858,  35920,  35982,  58550,  58612,  58674,
        58686,  58736,  58748,  58798,  58810,  58860,  58872,  58934,  58996,  59058,  59099,  59120,  59161,  59182,
        59244,  62573,  62574,  62635,  62636,  62697,  62698,  62759,  62760,  62821,  62822,  62883,  63155,  63156,
        70392,  70454,  70516,  70578,  93146,  93208,  93270,  93282,  93332,  93344,  93394,  93406,  93456,  93468,
        93530,  93592,  93654,  93695,  93716,  93757,  93778,  93840,  97169,  97170,  97231,  97232,  97293,  97294,
        97355,  97356,  97417,  97418,  97479,  97751,  97752,  104988, 105050, 105112, 105174, 127742, 127804, 127866,
        127878, 127928, 127940, 127990, 128002, 128052, 128064, 128126, 128188, 128250, 128291, 128312, 128353, 128374,
        128436, 131765, 131766, 131827, 131828, 131889, 131890, 131951, 131952, 132013, 132014, 132075, 132347, 132348};

    map<int, float> bbox_targets = {
        {192, 2.90271735},     {193, 2.81576085},     {194, 2.72880435},     {195, 2.64184785},     {196, 2.55489135},
        {197, 2.46793485},     {198, 2.38097835},     {199, 2.29402184},     {200, 2.20706511},     {201, 2.1201086},
        {202, 2.0331521},      {203, 1.9461956},      {204, 1.8592391},      {205, 1.7722826},      {206, 1.6853261},
        {207, 1.5983696},      {208, 1.5114131},      {209, 1.42445648},     {210, 1.33749998},     {211, 1.25054348},
        {212, 1.16358697},     {213, 1.07663047},     {214, 0.98967391},     {215, 0.90271741},     {216, 0.81576085},
        {217, 0.72880435},     {218, 0.64184785},     {219, 0.55489129},     {220, 0.46793479},     {221, 0.38097826},
        {222, 0.29402173},     {223, 0.20706522},     {224, 0.12010869},     {225, 0.03315217},     {226, -0.05380435},
        {227, -0.14076087},    {228, -0.22771738},    {229, -0.3146739},     {254, 2.90271735},     {255, 2.81576085},
        {256, 2.72880435},     {257, 2.64184785},     {258, 2.55489135},     {259, 2.46793485},     {260, 2.38097835},
        {261, 2.29402184},     {262, 2.20706511},     {263, 2.1201086},      {264, 2.0331521},      {265, 1.9461956},
        {266, 1.8592391},      {267, 1.7722826},      {268, 1.6853261},      {269, 1.5983696},      {270, 1.5114131},
        {271, 1.42445648},     {272, 1.33749998},     {273, 1.25054348},     {274, 1.16358697},     {275, 1.07663047},
        {276, 0.98967391},     {277, 0.90271741},     {278, 0.81576085},     {279, 0.72880435},     {280, 0.64184785},
        {281, 0.55489129},     {282, 0.46793479},     {283, 0.38097826},     {284, 0.29402173},     {285, 0.20706522},
        {286, 0.12010869},     {287, 0.03315217},     {288, -0.05380435},    {289, -0.14076087},    {290, -0.22771738},
        {291, -0.3146739},     {316, 2.90271735},     {317, 2.81576085},     {318, 2.72880435},     {319, 2.64184785},
        {320, 2.55489135},     {321, 2.46793485},     {322, 2.38097835},     {323, 2.29402184},     {324, 2.20706511},
        {325, 2.1201086},      {326, 2.0331521},      {327, 1.9461956},      {328, 1.8592391},      {329, 1.7722826},
        {330, 1.6853261},      {331, 1.5983696},      {332, 1.5114131},      {333, 1.42445648},     {334, 1.33749998},
        {335, 1.25054348},     {336, 1.16358697},     {337, 1.07663047},     {338, 0.98967391},     {339, 0.90271741},
        {340, 0.81576085},     {341, 0.72880435},     {342, 0.64184785},     {343, 0.55489129},     {344, 0.46793479},
        {345, 0.38097826},     {346, 0.29402173},     {347, 0.20706522},     {348, 0.12010869},     {349, 0.03315217},
        {350, -0.05380435},    {351, -0.14076087},    {352, -0.22771738},    {353, -0.3146739},     {378, 2.90271735},
        {379, 2.81576085},     {380, 2.72880435},     {381, 2.64184785},     {382, 2.55489135},     {383, 2.46793485},
        {384, 2.38097835},     {385, 2.29402184},     {386, 0.72880435},     {387, 0.64184785},     {388, 0.55489129},
        {389, 0.46793479},     {390, 0.38097826},     {391, 0.29402173},     {392, 0.20706522},     {393, 0.12010869},
        {394, 0.03315217},     {395, -0.05380435},    {396, -0.14076087},    {397, -0.22771738},    {398, -0.3146739},
        {399, -0.40163043},    {400, -0.48858696},    {401, -0.57554346},    {402, -0.66250002},    {403, -0.74945652},
        {404, 0.64184785},     {405, 0.55489129},     {406, 0.46793479},     {407, 0.38097826},     {408, 0.29402173},
        {409, 0.20706522},     {410, 0.12010869},     {411, 0.03315217},     {412, -0.05380435},    {413, -0.14076087},
        {414, -0.22771738},    {415, -0.3146739},     {440, 2.90271735},     {441, 2.81576085},     {442, 2.72880435},
        {443, 2.64184785},     {444, 2.55489135},     {445, 2.46793485},     {446, 2.38097835},     {447, 2.29402184},
        {448, 0.72880435},     {449, 0.64184785},     {450, 0.55489129},     {451, 0.46793479},     {452, 0.38097826},
        {453, 0.29402173},     {454, 0.20706522},     {455, 0.12010869},     {456, 0.03315217},     {457, -0.05380435},
        {458, -0.14076087},    {459, -0.22771738},    {460, -0.3146739},     {461, -0.40163043},    {462, -0.48858696},
        {463, -0.57554346},    {464, -0.66250002},    {465, -0.74945652},    {466, 0.64184785},     {467, 0.55489129},
        {468, 0.46793479},     {469, 0.38097826},     {470, 0.29402173},     {471, 0.20706522},     {472, 0.12010869},
        {473, 0.03315217},     {474, -0.05380435},    {475, -0.14076087},    {476, -0.22771738},    {477, -0.3146739},
        {502, 0.80271739},     {503, 0.71576089},     {504, 0.62880433},     {505, 0.54184783},     {506, 0.45489129},
        {507, 0.36793479},     {508, 0.28097826},     {509, 0.19402175},     {510, 0.10706522},     {511, 0.0201087},
        {512, 0.55489129},     {513, 0.46793479},     {514, 0.38097826},     {515, 0.29402173},     {516, 0.20706522},
        {517, 0.12010869},     {518, 0.03315217},     {519, -0.05380435},    {520, -0.14076087},    {521, -0.22771738},
        {522, -0.3146739},     {523, -0.40163043},    {524, -0.48858696},    {525, -0.57554346},    {526, -0.66250002},
        {527, 0.25923914},     {528, 0.17228261},     {529, 0.08532609},     {530, -0.00163043},    {531, -0.08858696},
        {532, -0.17554347},    {1200, -0.03641304},   {1262, -0.03641304},   {1324, -0.03641304},   {1386, -0.03641304},
        {23954, 0.06931818},   {24016, 0.06931818},   {24078, 0.06931818},   {24090, -0.00340909},  {24140, 0.06931818},
        {24152, -0.00340909},  {24202, 0.06931818},   {24214, -0.00340909},  {24264, 0.06931818},   {24276, -0.00340909},
        {24338, -0.00340909},  {24400, -0.00340909},  {24462, -0.00340909},  {24503, 0.06931818},   {24524, -0.00340909},
        {24565, 0.06931818},   {24586, -0.00340909},  {24648, -0.00340909},  {27977, 0.02102273},   {27978, -0.06988636},
        {28039, 0.02102273},   {28040, -0.06988636},  {28101, 0.02102273},   {28102, -0.06988636},  {28163, 0.02102273},
        {28164, -0.06988636},  {28225, 0.02102273},   {28226, -0.06988636},  {28287, 0.02102273},   {28559, 0.03465909},
        {28560, -0.05625},     {34788, 4.2302084},    {34789, 4.2302084},    {34790, 4.2302084},    {34791, 4.2302084},
        {34792, 4.2302084},    {34793, 4.2302084},    {34794, 4.2302084},    {34795, 4.2302084},    {34796, 4.2302084},
        {34797, 4.2302084},    {34798, 4.2302084},    {34799, 4.2302084},    {34800, 4.2302084},    {34801, 4.2302084},
        {34802, 4.2302084},    {34803, 4.2302084},    {34804, 4.2302084},    {34805, 4.2302084},    {34806, 4.2302084},
        {34807, 4.2302084},    {34808, 4.2302084},    {34809, 4.2302084},    {34810, 4.2302084},    {34811, 4.2302084},
        {34812, 4.2302084},    {34813, 4.2302084},    {34814, 4.2302084},    {34815, 4.2302084},    {34816, 4.2302084},
        {34817, 4.2302084},    {34818, 4.2302084},    {34819, 4.2302084},    {34820, 4.2302084},    {34821, 4.2302084},
        {34822, 4.2302084},    {34823, 4.2302084},    {34824, 4.2302084},    {34825, 4.2302084},    {34850, 4.06354189},
        {34851, 4.06354189},   {34852, 4.06354189},   {34853, 4.06354189},   {34854, 4.06354189},   {34855, 4.06354189},
        {34856, 4.06354189},   {34857, 4.06354189},   {34858, 4.06354189},   {34859, 4.06354189},   {34860, 4.06354189},
        {34861, 4.06354189},   {34862, 4.06354189},   {34863, 4.06354189},   {34864, 4.06354189},   {34865, 4.06354189},
        {34866, 4.06354189},   {34867, 4.06354189},   {34868, 4.06354189},   {34869, 4.06354189},   {34870, 4.06354189},
        {34871, 4.06354189},   {34872, 4.06354189},   {34873, 4.06354189},   {34874, 4.06354189},   {34875, 4.06354189},
        {34876, 4.06354189},   {34877, 4.06354189},   {34878, 4.06354189},   {34879, 4.06354189},   {34880, 4.06354189},
        {34881, 4.06354189},   {34882, 4.06354189},   {34883, 4.06354189},   {34884, 4.06354189},   {34885, 4.06354189},
        {34886, 4.06354189},   {34887, 4.06354189},   {34912, 3.8968749},    {34913, 3.8968749},    {34914, 3.8968749},
        {34915, 3.8968749},    {34916, 3.8968749},    {34917, 3.8968749},    {34918, 3.8968749},    {34919, 3.8968749},
        {34920, 3.8968749},    {34921, 3.8968749},    {34922, 3.8968749},    {34923, 3.8968749},    {34924, 3.8968749},
        {34925, 3.8968749},    {34926, 3.8968749},    {34927, 3.8968749},    {34928, 3.8968749},    {34929, 3.8968749},
        {34930, 3.8968749},    {34931, 3.8968749},    {34932, 3.8968749},    {34933, 3.8968749},    {34934, 3.8968749},
        {34935, 3.8968749},    {34936, 3.8968749},    {34937, 3.8968749},    {34938, 3.8968749},    {34939, 3.8968749},
        {34940, 3.8968749},    {34941, 3.8968749},    {34942, 3.8968749},    {34943, 3.8968749},    {34944, 3.8968749},
        {34945, 3.8968749},    {34946, 3.8968749},    {34947, 3.8968749},    {34948, 3.8968749},    {34949, 3.8968749},
        {34974, 3.7302084},    {34975, 3.7302084},    {34976, 3.7302084},    {34977, 3.7302084},    {34978, 3.7302084},
        {34979, 3.7302084},    {34980, 3.7302084},    {34981, 3.7302084},    {34982, 1.75520837},   {34983, 1.75520837},
        {34984, 1.75520837},   {34985, 1.75520837},   {34986, 1.75520837},   {34987, 1.75520837},   {34988, 1.75520837},
        {34989, 1.75520837},   {34990, 1.75520837},   {34991, 1.75520837},   {34992, 1.75520837},   {34993, 1.75520837},
        {34994, 1.75520837},   {34995, 1.75520837},   {34996, 1.75520837},   {34997, 1.75520837},   {34998, 1.75520837},
        {34999, 1.75520837},   {35000, 3.7302084},    {35001, 3.7302084},    {35002, 3.7302084},    {35003, 3.7302084},
        {35004, 3.7302084},    {35005, 3.7302084},    {35006, 3.7302084},    {35007, 3.7302084},    {35008, 3.7302084},
        {35009, 3.7302084},    {35010, 3.7302084},    {35011, 3.7302084},    {35036, 3.56354165},   {35037, 3.56354165},
        {35038, 3.56354165},   {35039, 3.56354165},   {35040, 3.56354165},   {35041, 3.56354165},   {35042, 3.56354165},
        {35043, 3.56354165},   {35044, 1.58854163},   {35045, 1.58854163},   {35046, 1.58854163},   {35047, 1.58854163},
        {35048, 1.58854163},   {35049, 1.58854163},   {35050, 1.58854163},   {35051, 1.58854163},   {35052, 1.58854163},
        {35053, 1.58854163},   {35054, 1.58854163},   {35055, 1.58854163},   {35056, 1.58854163},   {35057, 1.58854163},
        {35058, 1.58854163},   {35059, 1.58854163},   {35060, 1.58854163},   {35061, 1.58854163},   {35062, 3.56354165},
        {35063, 3.56354165},   {35064, 3.56354165},   {35065, 3.56354165},   {35066, 3.56354165},   {35067, 3.56354165},
        {35068, 3.56354165},   {35069, 3.56354165},   {35070, 3.56354165},   {35071, 3.56354165},   {35072, 3.56354165},
        {35073, 3.56354165},   {35098, 1.86354172},   {35099, 1.86354172},   {35100, 1.86354172},   {35101, 1.86354172},
        {35102, 1.86354172},   {35103, 1.86354172},   {35104, 1.86354172},   {35105, 1.86354172},   {35106, 1.86354172},
        {35107, 1.86354172},   {35108, 1.421875},     {35109, 1.421875},     {35110, 1.421875},     {35111, 1.421875},
        {35112, 1.421875},     {35113, 1.421875},     {35114, 1.421875},     {35115, 1.421875},     {35116, 1.421875},
        {35117, 1.421875},     {35118, 1.421875},     {35119, 1.421875},     {35120, 1.421875},     {35121, 1.421875},
        {35122, 1.421875},     {35123, 2.12187505},   {35124, 2.12187505},   {35125, 2.12187505},   {35126, 2.12187505},
        {35127, 2.12187505},   {35128, 2.12187505},   {35796, 0.21354167},   {35858, 0.046875},     {35920, -0.11979166},
        {35982, -0.28645834},  {58550, 0.23011364},   {58612, 0.13920455},   {58674, 0.04829545},   {58686, 0.43011364},
        {58736, -0.04261364},  {58748, 0.33920455},   {58798, -0.13352273},  {58810, 0.24829546},   {58860, -0.22443181},
        {58872, 0.15738636},   {58934, 0.06647728},   {58996, -0.02443182},  {59058, -0.11534091},  {59099, 0.03011364},
        {59120, -0.20625},     {59161, -0.06079546},  {59182, -0.29715911},  {59244, -0.38806817},  {62573, 0.09914773},
        {62574, 0.09914773},   {62635, 0.05369318},   {62636, 0.05369318},   {62697, 0.00823864},   {62698, 0.00823864},
        {62759, -0.03721591},  {62760, -0.03721591},  {62821, -0.08267046},  {62822, -0.08267046},  {62883, -0.128125},
        {63155, 0.10823864},   {63156, 0.10823864},   {69384, -0.18449783},  {69385, -0.18449783},  {69386, -0.18449783},
        {69387, -0.18449783},  {69388, -0.18449783},  {69389, -0.18449783},  {69390, -0.18449783},  {69391, -0.18449783},
        {69392, -0.18449783},  {69393, -0.18449783},  {69394, -0.18449783},  {69395, -0.18449783},  {69396, -0.18449783},
        {69397, -0.18449783},  {69398, -0.18449783},  {69399, -0.18449783},  {69400, -0.18449783},  {69401, -0.18449783},
        {69402, -0.18449783},  {69403, -0.18449783},  {69404, -0.18449783},  {69405, -0.18449783},  {69406, -0.18449783},
        {69407, -0.18449783},  {69408, -0.18449783},  {69409, -0.18449783},  {69410, -0.18449783},  {69411, -0.18449783},
        {69412, -0.18449783},  {69413, -0.18449783},  {69414, -0.18449783},  {69415, -0.18449783},  {69416, -0.18449783},
        {69417, -0.18449783},  {69418, -0.18449783},  {69419, -0.18449783},  {69420, -0.18449783},  {69421, -0.18449783},
        {69446, -0.18449783},  {69447, -0.18449783},  {69448, -0.18449783},  {69449, -0.18449783},  {69450, -0.18449783},
        {69451, -0.18449783},  {69452, -0.18449783},  {69453, -0.18449783},  {69454, -0.18449783},  {69455, -0.18449783},
        {69456, -0.18449783},  {69457, -0.18449783},  {69458, -0.18449783},  {69459, -0.18449783},  {69460, -0.18449783},
        {69461, -0.18449783},  {69462, -0.18449783},  {69463, -0.18449783},  {69464, -0.18449783},  {69465, -0.18449783},
        {69466, -0.18449783},  {69467, -0.18449783},  {69468, -0.18449783},  {69469, -0.18449783},  {69470, -0.18449783},
        {69471, -0.18449783},  {69472, -0.18449783},  {69473, -0.18449783},  {69474, -0.18449783},  {69475, -0.18449783},
        {69476, -0.18449783},  {69477, -0.18449783},  {69478, -0.18449783},  {69479, -0.18449783},  {69480, -0.18449783},
        {69481, -0.18449783},  {69482, -0.18449783},  {69483, -0.18449783},  {69508, -0.18449783},  {69509, -0.18449783},
        {69510, -0.18449783},  {69511, -0.18449783},  {69512, -0.18449783},  {69513, -0.18449783},  {69514, -0.18449783},
        {69515, -0.18449783},  {69516, -0.18449783},  {69517, -0.18449783},  {69518, -0.18449783},  {69519, -0.18449783},
        {69520, -0.18449783},  {69521, -0.18449783},  {69522, -0.18449783},  {69523, -0.18449783},  {69524, -0.18449783},
        {69525, -0.18449783},  {69526, -0.18449783},  {69527, -0.18449783},  {69528, -0.18449783},  {69529, -0.18449783},
        {69530, -0.18449783},  {69531, -0.18449783},  {69532, -0.18449783},  {69533, -0.18449783},  {69534, -0.18449783},
        {69535, -0.18449783},  {69536, -0.18449783},  {69537, -0.18449783},  {69538, -0.18449783},  {69539, -0.18449783},
        {69540, -0.18449783},  {69541, -0.18449783},  {69542, -0.18449783},  {69543, -0.18449783},  {69544, -0.18449783},
        {69545, -0.18449783},  {69570, -0.18449783},  {69571, -0.18449783},  {69572, -0.18449783},  {69573, -0.18449783},
        {69574, -0.18449783},  {69575, -0.18449783},  {69576, -0.18449783},  {69577, -0.18449783},  {69578, -0.65685719},
        {69579, -0.65685719},  {69580, -0.65685719},  {69581, -0.65685719},  {69582, -0.65685719},  {69583, -0.65685719},
        {69584, -0.65685719},  {69585, -0.65685719},  {69586, -0.65685719},  {69587, -0.65685719},  {69588, -0.65685719},
        {69589, -0.65685719},  {69590, -0.65685719},  {69591, -0.65685719},  {69592, -0.65685719},  {69593, -0.65685719},
        {69594, -0.65685719},  {69595, -0.65685719},  {69596, -0.18449783},  {69597, -0.18449783},  {69598, -0.18449783},
        {69599, -0.18449783},  {69600, -0.18449783},  {69601, -0.18449783},  {69602, -0.18449783},  {69603, -0.18449783},
        {69604, -0.18449783},  {69605, -0.18449783},  {69606, -0.18449783},  {69607, -0.18449783},  {69632, -0.18449783},
        {69633, -0.18449783},  {69634, -0.18449783},  {69635, -0.18449783},  {69636, -0.18449783},  {69637, -0.18449783},
        {69638, -0.18449783},  {69639, -0.18449783},  {69640, -0.65685719},  {69641, -0.65685719},  {69642, -0.65685719},
        {69643, -0.65685719},  {69644, -0.65685719},  {69645, -0.65685719},  {69646, -0.65685719},  {69647, -0.65685719},
        {69648, -0.65685719},  {69649, -0.65685719},  {69650, -0.65685719},  {69651, -0.65685719},  {69652, -0.65685719},
        {69653, -0.65685719},  {69654, -0.65685719},  {69655, -0.65685719},  {69656, -0.65685719},  {69657, -0.65685719},
        {69658, -0.18449783},  {69659, -0.18449783},  {69660, -0.18449783},  {69661, -0.18449783},  {69662, -0.18449783},
        {69663, -0.18449783},  {69664, -0.18449783},  {69665, -0.18449783},  {69666, -0.18449783},  {69667, -0.18449783},
        {69668, -0.18449783},  {69669, -0.18449783},  {69694, -0.0945496},   {69695, -0.0945496},   {69696, -0.0945496},
        {69697, -0.0945496},   {69698, -0.0945496},   {69699, -0.0945496},   {69700, -0.0945496},   {69701, -0.0945496},
        {69702, -0.0945496},   {69703, -0.0945496},   {69704, -0.65685719},  {69705, -0.65685719},  {69706, -0.65685719},
        {69707, -0.65685719},  {69708, -0.65685719},  {69709, -0.65685719},  {69710, -0.65685719},  {69711, -0.65685719},
        {69712, -0.65685719},  {69713, -0.65685719},  {69714, -0.65685719},  {69715, -0.65685719},  {69716, -0.65685719},
        {69717, -0.65685719},  {69718, -0.65685719},  {69719, -1.01623118},  {69720, -1.01623118},  {69721, -1.01623118},
        {69722, -1.01623118},  {69723, -1.01623118},  {69724, -1.01623118},  {70392, -0.01202858},  {70454, -0.01202858},
        {70516, -0.01202858},  {70578, -0.01202858},  {93146, 0.08074176},   {93208, 0.08074176},   {93270, 0.08074176},
        {93282, -0.27863222},  {93332, 0.08074176},   {93344, -0.27863222},  {93394, 0.08074176},   {93406, -0.27863222},
        {93456, 0.08074176},   {93468, -0.27863222},  {93530, -0.27863222},  {93592, -0.27863222},  {93654, -0.27863222},
        {93695, 0.17662354},   {93716, -0.27863222},  {93757, 0.17662354},   {93778, -0.27863222},  {93840, -0.27863222},
        {97169, -0.05009784},  {97170, -0.05009784},  {97231, -0.05009784},  {97232, -0.05009784},  {97293, -0.05009784},
        {97294, -0.05009784},  {97355, -0.05009784},  {97356, -0.05009784},  {97417, -0.05009784},  {97418, -0.05009784},
        {97479, -0.05009784},  {97751, -0.14004607},  {97752, -0.14004607},  {103980, 1.01538169},  {103981, 1.01538169},
        {103982, 1.01538169},  {103983, 1.01538169},  {103984, 1.01538169},  {103985, 1.01538169},  {103986, 1.01538169},
        {103987, 1.01538169},  {103988, 1.01538169},  {103989, 1.01538169},  {103990, 1.01538169},  {103991, 1.01538169},
        {103992, 1.01538169},  {103993, 1.01538169},  {103994, 1.01538169},  {103995, 1.01538169},  {103996, 1.01538169},
        {103997, 1.01538169},  {103998, 1.01538169},  {103999, 1.01538169},  {104000, 1.01538169},  {104001, 1.01538169},
        {104002, 1.01538169},  {104003, 1.01538169},  {104004, 1.01538169},  {104005, 1.01538169},  {104006, 1.01538169},
        {104007, 1.01538169},  {104008, 1.01538169},  {104009, 1.01538169},  {104010, 1.01538169},  {104011, 1.01538169},
        {104012, 1.01538169},  {104013, 1.01538169},  {104014, 1.01538169},  {104015, 1.01538169},  {104016, 1.01538169},
        {104017, 1.01538169},  {104042, 1.01538169},  {104043, 1.01538169},  {104044, 1.01538169},  {104045, 1.01538169},
        {104046, 1.01538169},  {104047, 1.01538169},  {104048, 1.01538169},  {104049, 1.01538169},  {104050, 1.01538169},
        {104051, 1.01538169},  {104052, 1.01538169},  {104053, 1.01538169},  {104054, 1.01538169},  {104055, 1.01538169},
        {104056, 1.01538169},  {104057, 1.01538169},  {104058, 1.01538169},  {104059, 1.01538169},  {104060, 1.01538169},
        {104061, 1.01538169},  {104062, 1.01538169},  {104063, 1.01538169},  {104064, 1.01538169},  {104065, 1.01538169},
        {104066, 1.01538169},  {104067, 1.01538169},  {104068, 1.01538169},  {104069, 1.01538169},  {104070, 1.01538169},
        {104071, 1.01538169},  {104072, 1.01538169},  {104073, 1.01538169},  {104074, 1.01538169},  {104075, 1.01538169},
        {104076, 1.01538169},  {104077, 1.01538169},  {104078, 1.01538169},  {104079, 1.01538169},  {104104, 1.01538169},
        {104105, 1.01538169},  {104106, 1.01538169},  {104107, 1.01538169},  {104108, 1.01538169},  {104109, 1.01538169},
        {104110, 1.01538169},  {104111, 1.01538169},  {104112, 1.01538169},  {104113, 1.01538169},  {104114, 1.01538169},
        {104115, 1.01538169},  {104116, 1.01538169},  {104117, 1.01538169},  {104118, 1.01538169},  {104119, 1.01538169},
        {104120, 1.01538169},  {104121, 1.01538169},  {104122, 1.01538169},  {104123, 1.01538169},  {104124, 1.01538169},
        {104125, 1.01538169},  {104126, 1.01538169},  {104127, 1.01538169},  {104128, 1.01538169},  {104129, 1.01538169},
        {104130, 1.01538169},  {104131, 1.01538169},  {104132, 1.01538169},  {104133, 1.01538169},  {104134, 1.01538169},
        {104135, 1.01538169},  {104136, 1.01538169},  {104137, 1.01538169},  {104138, 1.01538169},  {104139, 1.01538169},
        {104140, 1.01538169},  {104141, 1.01538169},  {104166, 1.01538169},  {104167, 1.01538169},  {104168, 1.01538169},
        {104169, 1.01538169},  {104170, 1.01538169},  {104171, 1.01538169},  {104172, 1.01538169},  {104173, 1.01538169},
        {104174, 1.03333271},  {104175, 1.03333271},  {104176, 1.03333271},  {104177, 1.03333271},  {104178, 1.03333271},
        {104179, 1.03333271},  {104180, 1.03333271},  {104181, 1.03333271},  {104182, 1.03333271},  {104183, 1.03333271},
        {104184, 1.03333271},  {104185, 1.03333271},  {104186, 1.03333271},  {104187, 1.03333271},  {104188, 1.03333271},
        {104189, 1.03333271},  {104190, 1.03333271},  {104191, 1.03333271},  {104192, 1.01538169},  {104193, 1.01538169},
        {104194, 1.01538169},  {104195, 1.01538169},  {104196, 1.01538169},  {104197, 1.01538169},  {104198, 1.01538169},
        {104199, 1.01538169},  {104200, 1.01538169},  {104201, 1.01538169},  {104202, 1.01538169},  {104203, 1.01538169},
        {104228, 1.01538169},  {104229, 1.01538169},  {104230, 1.01538169},  {104231, 1.01538169},  {104232, 1.01538169},
        {104233, 1.01538169},  {104234, 1.01538169},  {104235, 1.01538169},  {104236, 1.03333271},  {104237, 1.03333271},
        {104238, 1.03333271},  {104239, 1.03333271},  {104240, 1.03333271},  {104241, 1.03333271},  {104242, 1.03333271},
        {104243, 1.03333271},  {104244, 1.03333271},  {104245, 1.03333271},  {104246, 1.03333271},  {104247, 1.03333271},
        {104248, 1.03333271},  {104249, 1.03333271},  {104250, 1.03333271},  {104251, 1.03333271},  {104252, 1.03333271},
        {104253, 1.03333271},  {104254, 1.01538169},  {104255, 1.01538169},  {104256, 1.01538169},  {104257, 1.01538169},
        {104258, 1.01538169},  {104259, 1.01538169},  {104260, 1.01538169},  {104261, 1.01538169},  {104262, 1.01538169},
        {104263, 1.01538169},  {104264, 1.01538169},  {104265, 1.01538169},  {104290, 1.10759962},  {104291, 1.10759962},
        {104292, 1.10759962},  {104293, 1.10759962},  {104294, 1.10759962},  {104295, 1.10759962},  {104296, 1.10759962},
        {104297, 1.10759962},  {104298, 1.10759962},  {104299, 1.10759962},  {104300, 1.03333271},  {104301, 1.03333271},
        {104302, 1.03333271},  {104303, 1.03333271},  {104304, 1.03333271},  {104305, 1.03333271},  {104306, 1.03333271},
        {104307, 1.03333271},  {104308, 1.03333271},  {104309, 1.03333271},  {104310, 1.03333271},  {104311, 1.03333271},
        {104312, 1.03333271},  {104313, 1.03333271},  {104314, 1.03333271},  {104315, 1.23656094},  {104316, 1.23656094},
        {104317, 1.23656094},  {104318, 1.23656094},  {104319, 1.23656094},  {104320, 1.23656094},  {104988, 0.52694499},
        {105050, 0.52694499},  {105112, 0.52694499},  {105174, 0.52694499},  {127742, 0.42719695},  {127804, 0.42719695},
        {127866, 0.42719695},  {127878, 0.63042521},  {127928, 0.42719695},  {127940, 0.63042521},  {127990, 0.42719695},
        {128002, 0.63042521},  {128052, 0.42719695},  {128064, 0.63042521},  {128126, 0.63042521},  {128188, 0.63042521},
        {128250, 0.63042521},  {128291, 0.17185026},  {128312, 0.63042521},  {128353, 0.17185026},  {128374, 0.63042521},
        {128436, 0.63042521},  {131765, -0.19168343}, {131766, -0.19168343}, {131827, -0.19168343}, {131828, -0.19168343},
        {131889, -0.19168343}, {131890, -0.19168343}, {131951, -0.19168343}, {131952, -0.19168343}, {132013, -0.19168343},
        {132014, -0.19168343}, {132075, -0.19168343}, {132347, -0.28390136}, {132348, -0.28390136}};

    // These two tables were generated with model private-neon/examples/rpn
    // random choice was disabled
    vector<int> fg_idx = {1200,  1262,  1324,  1386,  23954, 24016, 24078, 24090, 24140, 24152, 24202, 24214,
                          24264, 24276, 24338, 24400, 24462, 24503, 24524, 24565, 24586, 24648, 27977, 27978,
                          28039, 28040, 28101, 28102, 28163, 28164, 28225, 28226, 28287, 28559, 28560};

    vector<int> bg_idx = {
        192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216,
        217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 254, 255, 256, 257, 258, 259, 260, 261, 262, 263, 264, 265,
        266, 267, 268, 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290,
        291, 316, 317, 318, 319, 320, 321, 322, 323, 324, 325, 326, 327, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339,
        340, 341, 342, 343, 344, 345, 346, 347, 348, 349, 350, 351, 352, 353, 378, 379, 380, 381, 382, 383, 384, 385, 386, 387, 388,
        389, 390, 391, 392, 393, 394, 395, 396, 397, 398, 399, 400, 401, 402, 403, 404, 405, 406, 407, 408, 409, 410, 411, 412, 413,
        414, 415, 440, 441, 442, 443, 444, 445, 446, 447, 448, 449, 450, 451, 452, 453, 454, 455, 456, 457, 458, 459, 460, 461, 462,
        463, 464, 465, 466, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 502, 503, 504, 505, 506, 507, 508, 509, 510, 511,
        512, 513, 514, 515, 516, 517, 518, 519, 520, 521, 522, 523, 524, 525, 526, 527, 528, 529, 530, 531, 532};

    // bg and fg
    vector<int> labels_mask_idx = {
        192,   193,   194,   195,   196,   197,   198,   199,   200,   201,   202,   203,   204,   205,   206,   207,   208,
        209,   210,   211,   212,   213,   214,   215,   216,   217,   218,   219,   220,   221,   222,   223,   224,   225,
        226,   227,   228,   229,   254,   255,   256,   257,   258,   259,   260,   261,   262,   263,   264,   265,   266,
        267,   268,   269,   270,   271,   272,   273,   274,   275,   276,   277,   278,   279,   280,   281,   282,   283,
        284,   285,   286,   287,   288,   289,   290,   291,   316,   317,   318,   319,   320,   321,   322,   323,   324,
        325,   326,   327,   328,   329,   330,   331,   332,   333,   334,   335,   336,   337,   338,   339,   340,   341,
        342,   343,   344,   345,   346,   347,   348,   349,   350,   351,   352,   353,   378,   379,   380,   381,   382,
        383,   384,   385,   386,   387,   388,   389,   390,   391,   392,   393,   394,   395,   396,   397,   398,   399,
        400,   401,   402,   403,   404,   405,   406,   407,   408,   409,   410,   411,   412,   413,   414,   415,   440,
        441,   442,   443,   444,   445,   446,   447,   448,   449,   450,   451,   452,   453,   454,   455,   456,   457,
        458,   459,   460,   461,   462,   463,   464,   465,   466,   467,   468,   469,   470,   471,   472,   473,   474,
        475,   476,   477,   502,   503,   504,   505,   506,   507,   508,   509,   510,   511,   512,   513,   514,   515,
        516,   517,   518,   519,   520,   521,   522,   523,   524,   525,   526,   527,   528,   529,   530,   531,   532,
        1200,  1262,  1324,  1386,  23954, 24016, 24078, 24090, 24140, 24152, 24202, 24214, 24264, 24276, 24338, 24400, 24462,
        24503, 24524, 24565, 24586, 24648, 27977, 27978, 28039, 28040, 28101, 28102, 28163, 28164, 28225, 28226, 28287, 28559,
        28560, 34788, 34789, 34790, 34791, 34792, 34793, 34794, 34795, 34796, 34797, 34798, 34799, 34800, 34801, 34802, 34803,
        34804, 34805, 34806, 34807, 34808, 34809, 34810, 34811, 34812, 34813, 34814, 34815, 34816, 34817, 34818, 34819, 34820,
        34821, 34822, 34823, 34824, 34825, 34850, 34851, 34852, 34853, 34854, 34855, 34856, 34857, 34858, 34859, 34860, 34861,
        34862, 34863, 34864, 34865, 34866, 34867, 34868, 34869, 34870, 34871, 34872, 34873, 34874, 34875, 34876, 34877, 34878,
        34879, 34880, 34881, 34882, 34883, 34884, 34885, 34886, 34887, 34912, 34913, 34914, 34915, 34916, 34917, 34918, 34919,
        34920, 34921, 34922, 34923, 34924, 34925, 34926, 34927, 34928, 34929, 34930, 34931, 34932, 34933, 34934, 34935, 34936,
        34937, 34938, 34939, 34940, 34941, 34942, 34943, 34944, 34945, 34946, 34947, 34948, 34949, 34974, 34975, 34976, 34977,
        34978, 34979, 34980, 34981, 34982, 34983, 34984, 34985, 34986, 34987, 34988, 34989, 34990, 34991, 34992, 34993, 34994,
        34995, 34996, 34997, 34998, 34999, 35000, 35001, 35002, 35003, 35004, 35005, 35006, 35007, 35008, 35009, 35010, 35011,
        35036, 35037, 35038, 35039, 35040, 35041, 35042, 35043, 35044, 35045, 35046, 35047, 35048, 35049, 35050, 35051, 35052,
        35053, 35054, 35055, 35056, 35057, 35058, 35059, 35060, 35061, 35062, 35063, 35064, 35065, 35066, 35067, 35068, 35069,
        35070, 35071, 35072, 35073, 35098, 35099, 35100, 35101, 35102, 35103, 35104, 35105, 35106, 35107, 35108, 35109, 35110,
        35111, 35112, 35113, 35114, 35115, 35116, 35117, 35118, 35119, 35120, 35121, 35122, 35123, 35124, 35125, 35126, 35127,
        35128, 35796, 35858, 35920, 35982, 58550, 58612, 58674, 58686, 58736, 58748, 58798, 58810, 58860, 58872, 58934, 58996,
        59058, 59099, 59120, 59161, 59182, 59244, 62573, 62574, 62635, 62636, 62697, 62698, 62759, 62760, 62821, 62822, 62883,
        63155, 63156};

    string                    data         = read_file(CURDIR "/test_data/006637.json");
    auto                      image_config = make_image_config();
    localization::config      cfg          = make_localization_config(image_config);
    localization::extractor   extractor{cfg};
    localization::transformer transformer{cfg};
    localization::loader      loader{cfg};
    auto                      extract_data = extractor.extract(&data[0], data.size());
    ASSERT_NE(nullptr, extract_data);

    vector<unsigned char>      img = make_image_from_metadata(data);
    image::extractor           ext{image_config};
    shared_ptr<image::decoded> decoded = ext.extract((char*)&img[0], img.size());
    image::param_factory       factory(image_config);
    shared_ptr<image::params>  params = factory.make_params(decoded);
    params->debug_deterministic       = true;

    shared_ptr<localization::decoded> transformed_data = transformer.transform(params, extract_data);

    ASSERT_EQ(transformed_data->anchor_index.size(), fg_idx.size() + bg_idx.size());
    for (int i = 0; i < fg_idx.size(); i++)
    {
        ASSERT_EQ(fg_idx[i], transformed_data->anchor_index[i]);
    }
    for (int i = 0; i < bg_idx.size(); i++)
    {
        ASSERT_EQ(bg_idx[i], transformed_data->anchor_index[i + fg_idx.size()]);
    }

    vector<float>   bbtargets;
    vector<float>   bbtargets_mask;
    vector<int32_t> labels_flat;
    vector<int32_t> labels_mask;
    vector<int32_t> im_shape;
    vector<float>   gt_boxes;
    vector<int32_t> num_gt_boxes;
    vector<int32_t> gt_classes;
    vector<float>   im_scale;
    vector<int32_t> gt_difficult;

    vector<void*>             buf_list;
    const vector<shape_type>& shapes = cfg.get_shape_type_list();
    ASSERT_EQ(10, shapes.size());
    size_t total_anchors = 34596;

    bbtargets.resize(shapes[0].get_element_count());
    bbtargets_mask.resize(shapes[1].get_element_count());
    labels_flat.resize(shapes[2].get_element_count());
    labels_mask.resize(shapes[3].get_element_count());
    im_shape.resize(shapes[4].get_element_count());
    gt_boxes.resize(shapes[5].get_element_count());
    num_gt_boxes.resize(shapes[6].get_element_count());
    gt_classes.resize(shapes[7].get_element_count());
    im_scale.resize(shapes[8].get_element_count());
    gt_difficult.resize(shapes[9].get_element_count());

    ASSERT_EQ(total_anchors * 4, bbtargets.size());
    ASSERT_EQ(total_anchors * 4, bbtargets_mask.size());
    ASSERT_EQ(total_anchors * 2, labels_flat.size());
    ASSERT_EQ(total_anchors * 2, labels_mask.size());
    ASSERT_EQ(2, im_shape.size());
    ASSERT_EQ(64 * 4, gt_boxes.size());
    ASSERT_EQ(1, num_gt_boxes.size());
    ASSERT_EQ(64, gt_classes.size());
    ASSERT_EQ(1, im_scale.size());
    ASSERT_EQ(64, gt_difficult.size());

    memset(bbtargets.data(), 0xFF, bbtargets.size() * sizeof(float));
    memset(bbtargets_mask.data(), 0xFF, bbtargets_mask.size() * sizeof(float));
    memset(labels_flat.data(), 0xFF, labels_flat.size() * sizeof(int32_t));
    memset(labels_mask.data(), 0xFF, labels_mask.size() * sizeof(int32_t));
    memset(im_shape.data(), 0xFF, im_shape.size() * sizeof(int32_t));
    memset(gt_boxes.data(), 0xFF, gt_boxes.size() * sizeof(float));
    memset(num_gt_boxes.data(), 0xFF, num_gt_boxes.size() * sizeof(int32_t));
    memset(gt_classes.data(), 0xFF, gt_classes.size() * sizeof(int32_t));
    memset(im_scale.data(), 0xFF, im_scale.size() * sizeof(float));
    memset(gt_difficult.data(), 0xFF, gt_difficult.size() * sizeof(int32_t));

    buf_list.push_back(bbtargets.data());
    buf_list.push_back(bbtargets_mask.data());
    buf_list.push_back(labels_flat.data());
    buf_list.push_back(labels_mask.data());
    buf_list.push_back(im_shape.data());
    buf_list.push_back(gt_boxes.data());
    buf_list.push_back(num_gt_boxes.data());
    buf_list.push_back(gt_classes.data());
    buf_list.push_back(im_scale.data());
    buf_list.push_back(gt_difficult.data());

    loader.load(buf_list, transformed_data);

    //    loader.build_output(transformed_data, labels, labels_mask, bbtargets, bbtargets_mask);

    //-------------------------------------------------------------------------
    // labels
    //-------------------------------------------------------------------------
    for (size_t i = 0; i < labels_flat.size() / 2; i++)
    {
        auto p = find(fg_idx.begin(), fg_idx.end(), i);
        if (p != fg_idx.end())
        {
            ASSERT_EQ(0, labels_flat[i]) << "at index " << i;
            ASSERT_EQ(1, labels_flat[i + total_anchors]) << "at index " << i;
        }
        else
        {
            ASSERT_EQ(1, labels_flat[i]) << "at index " << i;
            ASSERT_EQ(0, labels_flat[i + total_anchors]) << "at index " << i;
        }
    }

    //-------------------------------------------------------------------------
    // labels_mask
    //-------------------------------------------------------------------------
    for (size_t i = 0; i < labels_mask.size(); i++)
    {
        auto p = find(labels_mask_idx.begin(), labels_mask_idx.end(), i);
        if (p != labels_mask_idx.end())
        {
            ASSERT_EQ(1, labels_mask[i]) << "at index " << i;
        }
        else
        {
            ASSERT_EQ(0, labels_mask[i]) << "at index " << i;
        }
    }

    //-------------------------------------------------------------------------
    // bbtargets
    //-------------------------------------------------------------------------
    for (int i = 0; i < bbtargets.size(); i++)
    {
        auto p = bbox_targets.find(i);
        if (p != bbox_targets.end())
        {
            ASSERT_NEAR(p->second, bbtargets[i], 0.000001) << "at index " << i;
        }
        else
        {
            ASSERT_EQ(0., bbtargets[i]) << "at index " << i;
        }
    }

    //-------------------------------------------------------------------------
    // bbtargets_mask
    //-------------------------------------------------------------------------
    for (int i = 0; i < bbtargets_mask.size() / 4; i++)
    {
        auto fg = find(fg_idx.begin(), fg_idx.end(), i);
        if (fg != fg_idx.end())
        {
            ASSERT_EQ(1, bbtargets_mask[i + total_anchors * 0]) << "at index " << i;
            ASSERT_EQ(1, bbtargets_mask[i + total_anchors * 1]) << "at index " << i;
            ASSERT_EQ(1, bbtargets_mask[i + total_anchors * 2]) << "at index " << i;
            ASSERT_EQ(1, bbtargets_mask[i + total_anchors * 3]) << "at index " << i;
        }
        else
        {
            ASSERT_EQ(0, bbtargets_mask[i + total_anchors * 0]) << "at index " << i;
            ASSERT_EQ(0, bbtargets_mask[i + total_anchors * 1]) << "at index " << i;
            ASSERT_EQ(0, bbtargets_mask[i + total_anchors * 2]) << "at index " << i;
            ASSERT_EQ(0, bbtargets_mask[i + total_anchors * 3]) << "at index " << i;
        }
    }

    EXPECT_EQ(800, im_shape[0]) << "width";
    EXPECT_EQ(600, im_shape[1]) << "height";
    EXPECT_EQ(6, num_gt_boxes[0]);
    for (int i = 0; i < num_gt_boxes[0]; i++)
    {
        const boundingbox::box& box = transformed_data->boxes()[i];
        EXPECT_EQ(box.xmin * im_scale[0], gt_boxes[i * 4 + 0]);
        EXPECT_EQ(box.ymin * im_scale[0], gt_boxes[i * 4 + 1]);
        EXPECT_EQ(box.xmax * im_scale[0], gt_boxes[i * 4 + 2]);
        EXPECT_EQ(box.ymax * im_scale[0], gt_boxes[i * 4 + 3]);
        EXPECT_EQ(box.label, gt_classes[i]);
        EXPECT_EQ(box.difficult, gt_difficult[i]);
    }
    EXPECT_FLOAT_EQ(1.6, im_scale[0]);
}

TEST(localization, loader_zero_gt_boxes)
{
    string data = read_file(CURDIR "/test_data/006637.json");

    nlohmann::json ijs          = {{"width", 1000}, {"height", 1000}, {"flip_enable", false}, {"scale", {0.1, 0.1}}};
    auto           image_config = image::config{ijs};

    localization::config      cfg = make_localization_config(image_config);
    localization::extractor   extractor{cfg};
    localization::transformer transformer{cfg};
    localization::loader      loader{cfg};
    auto                      extract_data = extractor.extract(&data[0], data.size());
    ASSERT_NE(nullptr, extract_data);

    vector<unsigned char>      img = make_image_from_metadata(data);
    image::extractor           ext{image_config};
    shared_ptr<image::decoded> decoded = ext.extract((char*)&img[0], img.size());
    image::param_factory       factory(image_config);
    shared_ptr<image::params>  params = factory.make_params(decoded);
    params->debug_deterministic       = true;
    params->cropbox.x                 = 0;
    params->cropbox.y                 = 0;

    shared_ptr<localization::decoded> transformed_data = transformer.transform(params, extract_data);
    ASSERT_EQ(0, transformed_data->gt_boxes.size());

    //    ASSERT_EQ(transformed_data->anchor_index.size(), fg_idx.size() + bg_idx.size());
    //    for(int i=0; i<fg_idx.size(); i++) {
    //        ASSERT_EQ(fg_idx[i], transformed_data->anchor_index[i]);
    //    }
    //    for(int i=0; i<bg_idx.size(); i++) {
    //        ASSERT_EQ(bg_idx[i], transformed_data->anchor_index[i+fg_idx.size()]);
    //    }

    vector<float>   bbtargets;
    vector<float>   bbtargets_mask;
    vector<int32_t> labels_flat;
    vector<int32_t> labels_mask;
    vector<int32_t> im_shape;
    vector<float>   gt_boxes;
    vector<int32_t> num_gt_boxes;
    vector<int32_t> gt_classes;
    vector<float>   im_scale;
    vector<int32_t> gt_difficult;

    vector<void*>             buf_list;
    const vector<shape_type>& shapes = cfg.get_shape_type_list();
    ASSERT_EQ(10, shapes.size());
    size_t total_anchors = 34596;

    bbtargets.resize(shapes[0].get_element_count());
    bbtargets_mask.resize(shapes[1].get_element_count());
    labels_flat.resize(shapes[2].get_element_count());
    labels_mask.resize(shapes[3].get_element_count());
    im_shape.resize(shapes[4].get_element_count());
    gt_boxes.resize(shapes[5].get_element_count());
    num_gt_boxes.resize(shapes[6].get_element_count());
    gt_classes.resize(shapes[7].get_element_count());
    im_scale.resize(shapes[8].get_element_count());
    gt_difficult.resize(shapes[9].get_element_count());

    ASSERT_EQ(total_anchors * 4, bbtargets.size());
    ASSERT_EQ(total_anchors * 4, bbtargets_mask.size());
    ASSERT_EQ(total_anchors * 2, labels_flat.size());
    ASSERT_EQ(total_anchors * 2, labels_mask.size());
    ASSERT_EQ(2, im_shape.size());
    ASSERT_EQ(64 * 4, gt_boxes.size());
    ASSERT_EQ(1, num_gt_boxes.size());
    ASSERT_EQ(64, gt_classes.size());
    ASSERT_EQ(1, im_scale.size());
    ASSERT_EQ(64, gt_difficult.size());

    memset(bbtargets.data(), 0xFF, bbtargets.size() * sizeof(float));
    memset(bbtargets_mask.data(), 0xFF, bbtargets_mask.size() * sizeof(float));
    memset(labels_flat.data(), 0xFF, labels_flat.size() * sizeof(int32_t));
    memset(labels_mask.data(), 0xFF, labels_mask.size() * sizeof(int32_t));
    memset(im_shape.data(), 0xFF, im_shape.size() * sizeof(int32_t));
    memset(gt_boxes.data(), 0xFF, gt_boxes.size() * sizeof(float));
    memset(num_gt_boxes.data(), 0xFF, num_gt_boxes.size() * sizeof(int32_t));
    memset(gt_classes.data(), 0xFF, gt_classes.size() * sizeof(int32_t));
    memset(im_scale.data(), 0xFF, im_scale.size() * sizeof(float));
    memset(gt_difficult.data(), 0xFF, gt_difficult.size() * sizeof(int32_t));

    buf_list.push_back(bbtargets.data());
    buf_list.push_back(bbtargets_mask.data());
    buf_list.push_back(labels_flat.data());
    buf_list.push_back(labels_mask.data());
    buf_list.push_back(im_shape.data());
    buf_list.push_back(gt_boxes.data());
    buf_list.push_back(num_gt_boxes.data());
    buf_list.push_back(gt_classes.data());
    buf_list.push_back(im_scale.data());
    buf_list.push_back(gt_difficult.data());

    loader.load(buf_list, transformed_data);
}

TEST(localization, compute_targets)
{
    // expected values generated via python localization example

    vector<box> gt_bb;
    vector<box> rp_bb;

    // ('gt_bb {0}', array([ 561.6,  329.6,  713.6,  593.6]))
    // ('rp_bb {1}', array([ 624.,  248.,  799.,  599.]))
    // xgt 638.1, rp 712.0, dx -0.419886363636
    // ygt 462.1, rp 424.0, dy  0.108238636364
    // wgt 153.0, rp 176.0, dw -0.140046073646
    // hgt 265.0, rp 352.0, dh -0.283901349612

    gt_bb.emplace_back(561.6, 329.6, 713.6, 593.6);
    rp_bb.emplace_back(624.0, 248.0, 799.0, 599.0);

    float dx_0_expected = -0.419886363636;
    float dy_0_expected = 0.108238636364;
    float dw_0_expected = -0.140046073646;
    float dh_0_expected = -0.283901349612;

    // ('gt_bb {0}', array([ 561.6,  329.6,  713.6,  593.6]))
    // ('rp_bb {1}', array([ 496.,  248.,  671.,  599.]))
    // xgt 638.1, rp 584.0, dx  0.307386363636
    // ygt 462.1, rp 424.0, dy  0.108238636364
    // wgt 153.0, rp 176.0, dw -0.140046073646
    // hgt 265.0, rp 352.0, dh -0.283901349612

    gt_bb.emplace_back(561.6, 329.6, 713.6, 593.6);
    rp_bb.emplace_back(496.0, 248.0, 671.0, 599.0);

    float dx_1_expected = 0.307386363636;
    float dy_1_expected = 0.108238636364;
    float dw_1_expected = -0.140046073646;
    float dh_1_expected = -0.283901349612;

    ASSERT_EQ(gt_bb.size(), rp_bb.size());

    vector<target> result = localization::transformer::compute_targets(gt_bb, rp_bb);
    ASSERT_EQ(result.size(), gt_bb.size());

    float acceptable_error = 0.00001;

    EXPECT_NEAR(dx_0_expected, result[0].dx, acceptable_error);
    EXPECT_NEAR(dy_0_expected, result[0].dy, acceptable_error);
    EXPECT_NEAR(dw_0_expected, result[0].dw, acceptable_error);
    EXPECT_NEAR(dh_0_expected, result[0].dh, acceptable_error);
    EXPECT_NEAR(dx_1_expected, result[1].dx, acceptable_error);
    EXPECT_NEAR(dy_1_expected, result[1].dy, acceptable_error);
    EXPECT_NEAR(dw_1_expected, result[1].dw, acceptable_error);
    EXPECT_NEAR(dh_1_expected, result[1].dh, acceptable_error);
}

TEST(localization, provider)
{
    nlohmann::json js = {{"type", "image,localization"},
                         {"image",
                          {{"height", 1000},
                           {"width", 1000},
                           {"channel_major", false},
                           {"flip_enable", true},
                           {"crop_enable", false},
                           {"fixed_aspect_ratio", true},
                           {"fixed_scaling_factor", 1.6}}},
                         {"localization", {{"max_gt_boxes", 64}, {"class_names", {"bicycle", "person"}}}}};

    shared_ptr<provider_interface> media   = provider_factory::create(js);
    const vector<shape_type>&      oshapes = media->get_oshapes();
    ASSERT_NE(nullptr, media);
    ASSERT_EQ(11, oshapes.size());

    string       target = read_file(CURDIR "/test_data/006637.json");
    vector<char> target_data;
    target_data.insert(target_data.begin(), target.begin(), target.end());
    // Image size is from the 006637.json target data file
    cv::Mat image(375, 500, CV_8UC3);
    image = cv::Scalar(50, 100, 200);
    vector<uint8_t> image_data;
    vector<char>    image_cdata;
    cv::imencode(".png", image, image_data);
    for (auto c : image_data)
    {
        image_cdata.push_back(c);
    };

    buffer_in_array in_buf(2);
    in_buf[0]->add_item(image_cdata);
    in_buf[1]->add_item(target_data);

    vector<size_t> output_sizes;
    for (const shape_type& shape : oshapes)
    {
        output_sizes.push_back(shape.get_byte_size());
    }
    buffer_out_array  out_buf(output_sizes, 1);
    const shape_type& image_shape = oshapes[0];

    media->provide(0, in_buf, out_buf);

    int     width    = image_shape.get_shape()[0];
    int     height   = image_shape.get_shape()[1];
    int     channels = image_shape.get_shape()[2];
    cv::Mat result(height, width, CV_8UC(channels), out_buf[0]->get_item(0));
    //    cv::imwrite("localization_provider_source.png", image);
    //    cv::imwrite("localization_provider.png", result);

    uint8_t* data = result.data;
    for (int row = 0; row < result.rows; row++)
    {
        for (int col = 0; col < result.cols; col++)
        {
            if (col < 800 && row < 600)
            {
                ASSERT_EQ(50, data[0]) << "row=" << row << ", col=" << col;
                ASSERT_EQ(100, data[1]) << "row=" << row << ", col=" << col;
                ASSERT_EQ(200, data[2]) << "row=" << row << ", col=" << col;
            }
            else
            {
                ASSERT_EQ(0, data[0]) << "row=" << row << ", col=" << col;
                ASSERT_EQ(0, data[1]) << "row=" << row << ", col=" << col;
                ASSERT_EQ(0, data[2]) << "row=" << row << ", col=" << col;
            }
            data += 3;
        }
    }
}

TEST(localization, provider_channel_major)
{
    nlohmann::json js = {{"type", "image,localization"},
                         {"image",
                          {{"height", 1000},
                           {"width", 1000},
                           {"channel_major", true},
                           {"flip_enable", true},
                           {"crop_enable", false},
                           {"fixed_aspect_ratio", true},
                           {"fixed_scaling_factor", 1.6}}},
                         {"localization", {{"max_gt_boxes", 64}, {"class_names", {"bicycle", "person"}}}}};

    shared_ptr<provider_interface> media   = provider_factory::create(js);
    const vector<shape_type>&      oshapes = media->get_oshapes();
    ASSERT_NE(nullptr, media);
    ASSERT_EQ(11, oshapes.size());

    string       target = read_file(CURDIR "/test_data/006637.json");
    vector<char> target_data;
    target_data.insert(target_data.begin(), target.begin(), target.end());
    // Image size is from the 006637.json target data file
    cv::Mat image(375, 500, CV_8UC3);
    image = cv::Scalar(50, 100, 200);
    vector<uint8_t> image_data;
    vector<char>    image_cdata;
    cv::imencode(".png", image, image_data);
    for (auto c : image_data)
    {
        image_cdata.push_back(c);
    };

    buffer_in_array in_buf(2);
    in_buf[0]->add_item(image_cdata);
    in_buf[1]->add_item(target_data);

    vector<size_t> output_sizes;
    for (const shape_type& shape : oshapes)
    {
        output_sizes.push_back(shape.get_byte_size());
    }
    buffer_out_array  out_buf(output_sizes, 1);
    const shape_type& image_shape = oshapes[0];

    media->provide(0, in_buf, out_buf);

    int     width  = image_shape.get_shape()[1];
    int     height = image_shape.get_shape()[2];
    cv::Mat result(height * 3, width, CV_8UC1, out_buf[0]->get_item(0));
    cv::imwrite("localization_provider_channel_major.png", result);
    uint8_t* data = result.data;
    for (int row = 0; row < result.rows; row++)
    {
        for (int col = 0; col < result.cols; col++)
        {
            if (col < 800)
            {
                if (row >= 0 && row < 600)
                {
                    ASSERT_EQ(50, *data) << "row=" << row << ", col=" << col;
                }
                else if (row >= 1000 && row < 1600)
                {
                    ASSERT_EQ(100, *data) << "row=" << row << ", col=" << col;
                }
                else if (row >= 2000 && row < 2600)
                {
                    ASSERT_EQ(200, *data) << "row=" << row << ", col=" << col;
                }
                else
                {
                    ASSERT_EQ(0, *data) << "row=" << row << ", col=" << col;
                }
            }
            else
            {
                ASSERT_EQ(0, *data) << "row=" << row << ", col=" << col;
            }
            data++;
        }
    }
}