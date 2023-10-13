// SPDX-License-Identifier: GPL-2.0-or-later
/*
   Copyright 2005, 2006 by Gerald Friedland, Kristian Jantz and Lars Knipping

   Conversion to C++ for Inkscape by Bob Jamison

   Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#include <cmath>
#include <cstdarg>
#include <unordered_map>
#include <algorithm>
#include <cstdlib>
#include <cassert>

#include "siox.h"
#include "async/progress.h"

namespace Inkscape {
namespace Trace {

//########################################################################
//#  S I O X    I M A G E
//########################################################################

SioxImage::SioxImage(Glib::RefPtr<Gdk::Pixbuf> const &buf)
{
    width = buf->get_width();
    height = buf->get_height();

    // Allocate data arrays.
    int size = width * height;
    pixdata.resize(size);
    cmdata.resize(size);

    int rowstride = buf->get_rowstride();
    int nchannels = buf->get_n_channels();
    auto data     = buf->get_pixels();

    // Copy pixel data.
    for (int y = 0; y < height; y++) {
        auto p = data + rowstride * y;
        for (int x = 0; x < width; x++) {
            uint32_t r = p[0];
            uint32_t g = p[1];
            uint32_t b = p[2];
            uint32_t a = nchannels == 3 ? 255 : p[3];
            pixdata[offset(x, y)] = (a << 24) | (r << 16) | (g << 8) | b;
            p += nchannels;
        }
    }

    // Zero confidence matrix.
    std::fill(cmdata.begin(), cmdata.end(), 0.0f);
}

Glib::RefPtr<Gdk::Pixbuf> SioxImage::getGdkPixbuf() const
{
    auto buf = Gdk::Pixbuf::create(Gdk::Colorspace::RGB, true, 8, width, height);

    int rowstride = buf->get_rowstride();
    int nchannels = buf->get_n_channels();
    auto data     = buf->get_pixels();

    for (int y = 0; y < height; y++) {
        auto p = data + rowstride * y;
        for (int x = 0; x < width; x++) {
            uint32_t rgb = pixdata[offset(x, y)];
            p[0] = (rgb >> 16) & 0xff; // r
            p[1] = (rgb >>  8) & 0xff; // g
            p[2] = (rgb      ) & 0xff; // b
            p[3] = (rgb >> 24) & 0xff; // a
            p += nchannels;
        }
    }

    return buf;
}

bool SioxImage::writePPM(char const *filename) const
{
    auto f = std::fopen(filename, "wb");
    if (!f) {
        return false;
    }

    std::fprintf(f, "P6 %u %u 255\n", width, height);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t rgb = pixdata[offset(x, y)];
            uint8_t r = (rgb >> 16) & 0xff;
            uint8_t g = (rgb >>  8) & 0xff;
            uint8_t b = (rgb      ) & 0xff;
            std::fputc(r, f);
            std::fputc(g, f);
            std::fputc(b, f);
        }
    }

    std::fclose(f);

    return true;
}

unsigned SioxImage::hash() const
{
    unsigned result = width * height;

    for (int i = 0; i < width * height; i++) {
        result = 3 * result + (unsigned)pixdata[i] + (unsigned)((1 << 16) * cmdata[i]);
    }

    return result;
}

//########################################################################
//#  S I O X
//########################################################################

namespace {

/**
 * Apply a function which updates each pixel depending on the value of its neighbours.
 */
template <typename F>
void apply_adjacent(float *cm, int xres, int yres, F f)
{
    for (int y = 0; y < yres; y++) {
        for (int x = 0; x < xres - 1; x++) {
            int idx = y * xres + x;
            f(cm[idx], cm[idx + 1]);
        }
    }
    for (int y = 0; y < yres; y++) {
        for (int x = xres - 1; x >= 1; x--) {
            int idx = y * xres + x;
            f(cm[idx], cm[idx - 1]);
        }
    }
    for (int y = 0; y < yres - 1; y++) {
        for (int x = 0; x < xres; x++) {
            int idx = y * xres + x;
            f(cm[idx], cm[idx + xres]);
        }
    }
    for (int y = yres - 1; y >= 1; y--) {
        for (int x = 0; x < xres; x++) {
            int idx = y * xres + x;
            f(cm[idx], cm[idx - xres]);
        }
    }
}

/**
 * Applies the morphological dilate operator.
 *
 * Can be used to close small holes in the given confidence matrix.
 */
void dilate(float *cm, int xres, int yres)
{
    apply_adjacent(cm, xres, yres, [] (float &a, float b) {
        if (b > a) {
            a = b;
        }
    });
}

/**
 * Applies the morphological erode operator.
 */
void erode(float *cm, int xres, int yres)
{
    apply_adjacent(cm, xres, yres, [] (float &a, float b) {
        if (b < a) {
            a = b;
        }
    });
}

/**
 * Multiplies matrix with the given scalar.
 */
void premultiplyMatrix(float alpha, float *cm, int cmSize)
{
    for (int i = 0; i < cmSize; i++) {
        cm[i] *= alpha;
    }
}

/**
 * Normalizes the matrix to values to [0..1].
 */
void normalizeMatrix(float *cm, int cmSize)
{
    float max = 0.0f;
    for (int i = 0; i < cmSize; i++) {
        if (cm[i] > max) {
            max = cm[i];
        }
    }

    if (max <= 0.0f || max == 1.0f) {
        return;
    }

    float alpha = 1.0f / max;
    premultiplyMatrix(alpha, cm, cmSize);
}

/**
 * Blurs confidence matrix with a given symmetrically weighted kernel.
 *
 * In the standard case confidence matrix entries are between 0...1 and
 * the weight factors sum up to 1.
 */
void smooth(float *cm, int xres, int yres, float f1, float f2, float f3)
{
    for (int y = 0; y < yres; y++) {
        for (int x = 0; x < xres - 2; x++) {
            int idx = y * xres + x;
            cm[idx] = f1 * cm[idx] + f2 * cm[idx + 1] + f3 * cm[idx + 2];
        }
    }
    for (int y = 0; y < yres; y++) {
        for (int x = xres - 1; x >= 2; x--) {
            int idx = y * xres + x;
            cm[idx] = f3 * cm[idx - 2] + f2 * cm[idx - 1] + f1 * cm[idx];
        }
    }
    for (int y = 0; y < yres - 2; y++) {
        for (int x = 0; x < xres; x++) {
            int idx = y * xres + x;
            cm[idx] = f1 * cm[idx] + f2 * cm[((y + 1) * xres) + x] + f3 * cm[((y + 2) * xres) + x];
        }
    }
    for (int y = yres - 1; y >= 2; y--) {
        for (int x = 0; x < xres; x++) {
            int idx = y * xres + x;
            cm[idx] = f3 * cm[((y - 2) * xres) + x] + f2 * cm[((y - 1) * xres) + x] + f1 * cm[idx];
        }
    }
}

/**
 * Squared Euclidean distance of p and q.
 */
float sqrEuclideanDist(float *p, int pSize, float *q)
{
    float sum = 0.0;
    for (int i = 0; i < pSize; i++) {
        float v = p[i] - q[i];
        sum += v * v;
    }
    return sum;
}

} // namespace

Siox::Siox(Async::Progress<double> &progress)
    : progress(&progress)
    , width(0)
    , height(0)
    , pixelCount(0)
    , image(nullptr)
    , cm(nullptr) {}

void Siox::error(std::string const &msg)
{
     g_warning("Siox error: %s\n", msg.c_str());
}

void Siox::trace(std::string const &msg)
{
    g_message("Siox: %s\n", msg.c_str());
}

SioxImage Siox::extractForeground(SioxImage const &originalImage, uint32_t backgroundFillColor)
{
    trace("### Start");

    init();

    SioxImage workImage = originalImage;

    // Fetch some info from the image.
    width      = workImage.getWidth();
    height     = workImage.getHeight();
    pixelCount = width * height;
    image      = workImage.getImageData();
    cm         = workImage.getConfidenceData();

    // Create labelField.
    auto labelField_storage = std::make_unique<int[]>(pixelCount);
    labelField = labelField_storage.get();

    trace("### Creating signatures");

    // Create color signatures.
    std::vector<CieLab> knownBg, knownFg;
    auto imageClab = std::make_unique<CieLab[]>(pixelCount);
    for (int i = 0; i < pixelCount; i++) {
        float conf = cm[i];
        uint32_t pix = image[i];
        CieLab lab = pix;
        imageClab[i] = lab;
        if (conf <= BACKGROUND_CONFIDENCE) {
            knownBg.emplace_back(lab);
        } else if (conf >= FOREGROUND_CONFIDENCE) {
            knownFg.emplace_back(lab);
        }
    }

    progress->report_or_throw(0.1);

    trace("knownBg:" + std::to_string(knownBg.size()) + " knownFg:" + std::to_string(knownFg.size()));

    std::vector<CieLab> bgSignature;
    colorSignature(knownBg, bgSignature, 3);

    progress->report_or_throw(0.2);

    std::vector<CieLab> fgSignature;
    colorSignature(knownFg, fgSignature, 3);

    // trace("### bgSignature:" + std::to_string(bgSignature.size()));

    if (bgSignature.empty()) {
        // segmentation impossible
        error("Signature size is < 1. Segmentation is impossible");
        throw Exception();
    }

    progress->report_or_throw(0.3);

    // classify using color signatures,
    // classification cached in hashmap for drb and speedup purposes
    trace("### Analyzing image");

    std::unordered_map<uint32_t, bool> hs;

    int progressResolution = pixelCount / 10;

    for (int i = 0; i < pixelCount; i++) {
        if (i % progressResolution == 0) {
            progress->report_or_throw(0.3 + 0.6 * i / pixelCount);
        }

        if (cm[i] >= FOREGROUND_CONFIDENCE) {
            cm[i] = CERTAIN_FOREGROUND_CONFIDENCE;
        } else if (cm[i] <= BACKGROUND_CONFIDENCE) {
            cm[i] = CERTAIN_BACKGROUND_CONFIDENCE;
        } else { // somewhere in between
            auto [it, inserted] = hs.emplace(image[i], false);
            if (inserted) {
                auto const &lab = imageClab[i];

                float minBg = std::numeric_limits<float>::max();
                for (auto const &s : bgSignature) {
                    minBg = std::min(minBg, CieLab::diffSq(lab, s));
                }

                float minFg;
                if (fgSignature.empty()) {
                    minFg = clusterSize;
                } else {
                    minFg = std::numeric_limits<float>::max();
                    for (auto const &s : fgSignature) {
                        minFg = std::min(minFg, CieLab::diffSq(lab, s));
                    }
                }

                it->second = minBg < minFg;
            }

            bool isBackground = it->second;
            cm[i] = isBackground ? CERTAIN_BACKGROUND_CONFIDENCE : CERTAIN_FOREGROUND_CONFIDENCE;
        }
    }

    hs.clear();
    imageClab.reset();

    trace("### postProcessing");

    // Postprocessing
    smooth(cm, width, height, 0.333f, 0.333f, 0.333f); // average
    normalizeMatrix(cm, pixelCount);
    erode(cm, width, height);
    keepOnlyLargeComponents(UNKNOWN_REGION_CONFIDENCE, 1.0/*sizeFactorToKeep*/);

    // for (int i = 0; i < 2/*smoothness*/; i++)
    //     smooth(cm, width, height, 0.333f, 0.333f, 0.333f); // average

    normalizeMatrix(cm, pixelCount);

    for (int i = 0; i < pixelCount; i++) {
        cm[i] = cm[i] >= UNKNOWN_REGION_CONFIDENCE
              ? CERTAIN_FOREGROUND_CONFIDENCE
              : CERTAIN_BACKGROUND_CONFIDENCE;
    }

    keepOnlyLargeComponents(UNKNOWN_REGION_CONFIDENCE, 1.5/*sizeFactorToKeep*/);
    fillColorRegions();
    dilate(cm, width, height);

    progress->report_or_throw(1.0);

    // We are done. Now clear everything but the background.
    for (int i = 0; i < pixelCount; i++) {
        if (cm[i] < FOREGROUND_CONFIDENCE) {
            image[i] = backgroundFillColor;
        }
    }

    trace("### Done");
    return workImage;
}

void Siox::init()
{
    limits[0] = 0.64f;
    limits[1] = 1.28f;
    limits[2] = 2.56f;

    float negLimits[3];
    negLimits[0] = -limits[0];
    negLimits[1] = -limits[1];
    negLimits[2] = -limits[2];

    clusterSize = sqrEuclideanDist(limits, 3, negLimits);
}

void Siox::colorSignatureStage1(CieLab *points,
                                unsigned leftBase,
                                unsigned rightBase,
                                unsigned recursionDepth,
                                unsigned *clusterCount,
                                unsigned dims)
{
    unsigned currentDim = recursionDepth % dims;
    CieLab point = points[leftBase];
    float min = point(currentDim);
    float max = min;

    for (unsigned i = leftBase + 1; i < rightBase; i++) {
        point = points[i];
        float curval = point(currentDim);
        if (curval < min) min = curval;
        if (curval > max) max = curval;
    }

    // Do the Rubner-rule split (sounds like a dance)
    if (max - min > limits[currentDim]) {
        float pivotPoint = (min + max) / 2.0; // average
        unsigned left  = leftBase;
        unsigned right = rightBase - 1;

        // partition points according to the dimension
        while (true) {
            while (true) {
                point = points[left];
                if (point(currentDim) > pivotPoint) {
                    break;
                }
                left++;
            }
            while (true) {
                point = points[right];
                if (point(currentDim) <= pivotPoint) {
                    break;
                }
                right--;
            }

            if (left > right) {
                break;
            }

            point = points[left];
            points[left] = points[right];
            points[right] = point;

            left++;
            right--;
        }

        // Recurse and create sub-trees
        colorSignatureStage1(points, leftBase, left, recursionDepth + 1, clusterCount, dims);
        colorSignatureStage1(points, left, rightBase, recursionDepth + 1, clusterCount, dims);

    } else {
        // create a leaf
        CieLab newpoint;

        newpoint.C = rightBase - leftBase;

        for (; leftBase < rightBase; leftBase++) {
            newpoint.add(points[leftBase]);
        }

        // printf("clusters:%d\n", *clusters);

        if (newpoint.C != 0) {
            newpoint.mul(1.0f / newpoint.C);
        }
        points[*clusterCount] = newpoint;
        (*clusterCount)++;
    }
}

void Siox::colorSignatureStage2(CieLab  *points,
                                unsigned leftBase,
                                unsigned rightBase,
                                unsigned recursionDepth,
                                unsigned *clusterCount,
                                float threshold,
                                unsigned dims)
{
    unsigned currentDim = recursionDepth % dims;
    CieLab point = points[leftBase];
    float min = point(currentDim);
    float max = min;

    for (unsigned i = leftBase+ 1; i < rightBase; i++) {
        point = points[i];
        float curval = point(currentDim);
        if (curval < min) min = curval;
        if (curval > max) max = curval;
    }

    // Do the Rubner-rule split (sounds like a dance)
    if (max - min > limits[currentDim]) {
        float pivotPoint = (min + max) / 2.0; //average
        unsigned left  = leftBase;
        unsigned right = rightBase - 1;

        // partition points according to the dimension
        while (true) {
            while (true) {
                point = points[left];
                if (point(currentDim) > pivotPoint) {
                    break;
                }
                left++;
            }
            while (true) {
                point = points[right];
                if (point(currentDim) <= pivotPoint) {
                    break;
                }
                right--;
            }

            if (left > right) {
                break;
            }

            point = points[left];
            points[left] = points[right];
            points[right] = point;

            left++;
            right--;
        }

        //# Recurse and create sub-trees
        colorSignatureStage2(points, leftBase, left, recursionDepth + 1, clusterCount, threshold, dims);
        colorSignatureStage2(points, left, rightBase, recursionDepth + 1, clusterCount, threshold, dims);

    } else {
        //### Create a leaf
        unsigned sum = 0;
        for (unsigned i = leftBase; i < rightBase; i++) {
            sum += points[i].C;
        }

        if (sum >= threshold) {
            float scale = rightBase - leftBase;
            CieLab newpoint;

            for (; leftBase < rightBase; leftBase++) {
                newpoint.add(points[leftBase]);
            }

            if (scale != 0.0) {
                newpoint.mul(1.0 / scale);
            }
            points[*clusterCount] = newpoint;
            (*clusterCount)++;
        }
    }
}

void Siox::colorSignature(std::vector<CieLab> const &inputVec,
                          std::vector<CieLab> &result,
                          unsigned dims)
{
    if (inputVec.empty()) { // no error. just don't do anything
        return;
    }

    unsigned length = inputVec.size();
    result = inputVec;

    unsigned stage1length = 0;
    colorSignatureStage1(result.data(), 0, length, 0, &stage1length, dims);

    unsigned stage2length = 0;
    colorSignatureStage2(result.data(), 0, stage1length, 0, &stage2length, length * 0.001, dims);

    result.resize(stage2length);
}

void Siox::keepOnlyLargeComponents(float threshold, double sizeFactorToKeep)
{
    for (int idx = 0; idx < pixelCount; idx++) {
        labelField[idx] = -1;
    }

    int curlabel  = 0;
    int maxregion = 0;
    int maxblob   = 0;

    // slow but easy to understand:
    std::vector<int> labelSizes;
    for (int i = 0; i < pixelCount; i++) {
        int regionCount = 0;
        if (labelField[i] == -1 && cm[i] >= threshold) {
            regionCount = depthFirstSearch(i, threshold, curlabel++);
            labelSizes.emplace_back(regionCount);
        }

        if (regionCount > maxregion) {
            maxregion = regionCount;
            maxblob   = curlabel-1;
        }
    }

    for (int i = 0; i < pixelCount; i++) {
        if (labelField[i] != -1) {
            // remove if the component is to small
            if (labelSizes[labelField[i]] * sizeFactorToKeep < maxregion) {
                cm[i] = CERTAIN_BACKGROUND_CONFIDENCE;
            }

            // add maxblob always to foreground
            if (labelField[i] == maxblob) {
                cm[i] = CERTAIN_FOREGROUND_CONFIDENCE;
            }
        }
    }
}

int Siox::depthFirstSearch(int startPos, float threshold, int curLabel)
{
    // stores positions of labeled pixels, where the neighbours
    // should still be checked for processing:

    // trace("startPos:%d threshold:%f curLabel:%d",
    //     startPos, threshold, curLabel);

    std::vector<int> pixelsToVisit;
    int componentSize = 0;

    if (labelField[startPos] == -1 && cm[startPos] >= threshold) {
        labelField[startPos] = curLabel;
        componentSize++;
        pixelsToVisit.emplace_back(startPos);
    }

    while (!pixelsToVisit.empty()) {
        int pos = pixelsToVisit[pixelsToVisit.size() - 1];
        pixelsToVisit.erase(pixelsToVisit.end() - 1);
        int x = pos % width;
        int y = pos / width;

        // check all four neighbours
        int left = pos - 1;
        if (x - 1 >= 0 && labelField[left] == -1 && cm[left] >= threshold) {
            labelField[left] = curLabel;
            componentSize++;
            pixelsToVisit.emplace_back(left);
        }

        int right = pos + 1;
        if (x + 1 < width && labelField[right] == -1 && cm[right] >= threshold) {
            labelField[right] = curLabel;
            componentSize++;
            pixelsToVisit.emplace_back(right);
        }

        int top = pos - width;
        if (y - 1 >= 0 && labelField[top] == -1 && cm[top] >= threshold) {
            labelField[top] = curLabel;
            componentSize++;
            pixelsToVisit.emplace_back(top);
        }

        int bottom = pos + width;
        if (y + 1 < height && labelField[bottom] == -1 && cm[bottom] >= threshold) {
            labelField[bottom] = curLabel;
            componentSize++;
            pixelsToVisit.emplace_back(bottom);
        }
    }

    return componentSize;
}

void Siox::fillColorRegions()
{
    for (int idx = 0; idx < pixelCount; idx++) {
        labelField[idx] = -1;
    }

    std::vector<int> pixelsToVisit;
    for (int i = 0; i < pixelCount; i++) { // for all pixels
        if (labelField[i] != -1 || cm[i] < UNKNOWN_REGION_CONFIDENCE) {
            continue; // already visited or bg
        }

        uint32_t origColor = image[i];
        int curLabel       = i+1;
        labelField[i]      = curLabel;
        cm[i]              = CERTAIN_FOREGROUND_CONFIDENCE;

        // int componentSize = 1;
        pixelsToVisit.emplace_back(i);
        // depth first search to fill region
        while (!pixelsToVisit.empty()) {
            int pos = pixelsToVisit[pixelsToVisit.size() - 1];
            pixelsToVisit.erase(pixelsToVisit.end() - 1);
            int x = pos % width;
            int y = pos / width;
            // check all four neighbours
            int left = pos - 1;
            if (x - 1 >= 0 && labelField[left] == -1 && CieLab::diff(image[left], origColor) < 1.0) {
                labelField[left] = curLabel;
                cm[left] = CERTAIN_FOREGROUND_CONFIDENCE;
                // ++componentSize;
                pixelsToVisit.emplace_back(left);
            }
            int right = pos + 1;
            if (x + 1 < width && labelField[right] == -1 && CieLab::diff(image[right], origColor) < 1.0) {
                labelField[right] = curLabel;
                cm[right] = CERTAIN_FOREGROUND_CONFIDENCE;
                // ++componentSize;
                pixelsToVisit.emplace_back(right);
            }
            int top = pos - width;
            if (y - 1 >= 0 && labelField[top] == -1 && CieLab::diff(image[top], origColor) < 1.0) {
                labelField[top] = curLabel;
                cm[top] = CERTAIN_FOREGROUND_CONFIDENCE;
                // ++componentSize;
                pixelsToVisit.emplace_back(top);
            }
            int bottom = pos + width;
            if (y + 1 < height && labelField[bottom] == -1 && CieLab::diff(image[bottom], origColor) < 1.0) {
                labelField[bottom] = curLabel;
                cm[bottom] = CERTAIN_FOREGROUND_CONFIDENCE;
                // ++componentSize;
                pixelsToVisit.emplace_back(bottom);
            }
        }
    }
}

} // namespace Trace
} // namespace Inkscape
