#include "edges_pose_refiner/edgeModel.hpp"
#include "edges_pose_refiner/pclProcessing.hpp"
#include <pcl/registration/icp_nl.h>
#include <boost/make_shared.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/core/eigen.hpp>

#ifdef USE_3D_VISUALIZATION
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/visualization/cloud_viewer.h>
#include <boost/thread/thread.hpp>
#endif

using namespace cv;
using std::cout;
using std::endl;

//#define VISUALIZE_ALL_GENERATED_SILHOUETTES
//#define VISUALIZE_SILHOUETTE_GENERATION
//#define VISUALIZE_EDGE_MODEL_CREATION

void EdgeModel::projectPointsOnAxis(const EdgeModel &edgeModel, Point3d axis, vector<float> &projections, Point3d &center_d)
{
  Mat rvec, tvec;
  getRvecTvec(edgeModel.Rt_obj2cam, rvec, tvec);
  center_d = Point3d(tvec.reshape(1, 3));
  Point3f center_f = center_d;

  projections.resize(edgeModel.points.size());
  for(size_t i=0; i<edgeModel.points.size(); i++)
  {
    projections[i] = (edgeModel.points[i] - center_f).dot(axis);
  }
}

void EdgeModel::setTableAnchor(EdgeModel &edgeModel, float belowTableRatio)
{
  const float eps = 1e-4;
  CV_Assert(fabs(norm(edgeModel.upStraightDirection) - 1.0) < eps);
  vector<float> projections;
  Point3d center_d;
  projectPointsOnAxis(edgeModel, edgeModel.upStraightDirection, projections, center_d);

  int anchorIdx = belowTableRatio * projections.size();
  nth_element(projections.begin(), projections.begin() + anchorIdx, projections.end());
  float proj = projections[anchorIdx];

  edgeModel.tableAnchor = center_d + proj * edgeModel.upStraightDirection;
}

void EdgeModel::setStableEdgels(EdgeModel &edgeModel, float stableEdgelsRatio)
{
  const float eps = 1e-4;
  CV_Assert(fabs(norm(edgeModel.upStraightDirection) - 1.0) < eps);

  vector<float> projections;
  Point3d center_d;
  projectPointsOnAxis(edgeModel, edgeModel.upStraightDirection, projections, center_d);

  vector<float> projectionsBackup = projections;

  int thredholdIdx = stableEdgelsRatio * projections.size();
  nth_element(projections.begin(), projections.begin() + thredholdIdx, projections.end());
  float proj = projections[thredholdIdx];

  edgeModel.stableEdgels.clear();
  for (size_t i = 0; i < edgeModel.points.size(); ++i)
  {
    if (projectionsBackup[i] > proj)
    {
      edgeModel.stableEdgels.push_back(edgeModel.points[i]);
    }
  }
}

EdgeModel::EdgeModel()
{
}

EdgeModel::EdgeModel(const std::vector<cv::Point3f> &_points, const std::vector<cv::Point3f> &_normals, bool isModelUpsideDown, bool centralize, const EdgeModelCreationParams &_params)
{
  params = _params;

  EdgeModel inModel;
  Point3d axis(0.0, 0.0, 1.0);
  inModel.hasRotationSymmetry = isAxisCorrect(_points, axis, params.neighborIndex, params.distanceFactor, params.rotationCount);
  inModel.upStraightDirection = axis;
  inModel.points = _points;
  inModel.normals = _normals;

  computeObjectSystem(inModel.points, inModel.Rt_obj2cam);

  Point3d objectCenter = inModel.getObjectCenter();
  Mat tvec;
  point2col(objectCenter, tvec);

  const int dim = 3;
  Mat zeros = Mat::zeros(dim, 1, CV_64FC1);
  EdgeModel centralizedModel;
  if (centralize)
  {
    inModel.rotate_cam(PoseRT(zeros, -tvec), centralizedModel);
  }
  else
  {
    centralizedModel = inModel;
  }
  if (isModelUpsideDown)
  {
    centralizedModel.upStraightDirection *= -1;
  }
  setTableAnchor(centralizedModel, params.belowTableRatio);
  setStableEdgels(centralizedModel, params.stableEdgelsRatio);

  EdgeModel outModel;
  if (centralize)
  {
    point2col(centralizedModel.tableAnchor, tvec);
    tvec.at<double>(0) = 0.0;
    tvec.at<double>(1) = 0.0;
    centralizedModel.rotate_cam(PoseRT(zeros, -tvec) , outModel);
    Mat R = Mat::eye(dim, dim, outModel.Rt_obj2cam.type());
    Mat rotationRoi = outModel.Rt_obj2cam(Range(0, dim), Range(0, dim));
    R.copyTo(rotationRoi);
  }
  else
  {
    outModel = centralizedModel;
  }

  *this = outModel;
  cout << "normals size: " << normals.size() << endl;
}

EdgeModel::EdgeModel(const std::vector<cv::Point3f> &_points, bool isModelUpsideDown, bool centralize, const EdgeModelCreationParams &_params)
{
  CV_Assert(false);
  /*
   * Estimate normals

  pcl::PointCloud<pcl::PointXYZ> pclPoints;
  cv2pcl(inModel.points, pclPoints);
  pcl::PointCloud<pcl::Normal> pclNormals;

  cout << "size: " << pclPoints.points.size() << endl;
  estimateNormals(params.kSearch, pclPoints, pclNormals);
  inModel.normals.resize(pclNormals.size());
  for (size_t i = 0; i < pclNormals.points.size(); ++i)
  {
    Point3f currentNormal;
    currentNormal.x = pclNormals.points[i].normal[0];
    currentNormal.y = pclNormals.points[i].normal[1];
    currentNormal.z = pclNormals.points[i].normal[2];
    cout << pclNormals.points[i].curvature << endl;
    inModel.normals[i] = currentNormal;
  }
  */

/*
  boost::shared_ptr<pcl::visualization::PCLVisualizer> viewer (new pcl::visualization::PCLVisualizer ("3D Viewer"));
//    viewer->setBackgroundColor (0, 0, 0);
  pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> pointsColor(pclPoints.makeShared(), 0, 255, 0);
  viewer->addPointCloud<pcl::PointXYZ>(pclPoints.makeShared(), pointsColor, "tmp");
//    viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "sample cloud");
//    viewer->addPointCloudNormals<pcl::PointXYZ, pcl::Normal> (pclPoints.makeShared(), pclNormals, 10, 0.05, "normals");
    viewer->addPointCloudNormals<pcl::PointXYZ, pcl::Normal> (pclPoints.makeShared(), pclNormals.makeShared(), 10, 0.05, "normals");
//    viewer->addCoordinateSystem (1.0);
//    viewer->initCameraParameters ();

    while (!viewer->wasStopped ())
    {
      viewer->spinOnce (100);
//      boost::this_thread::sleep (boost::posix_time::microseconds (100000));
    }
*/
}

EdgeModel::EdgeModel(const EdgeModel &edgeModel)
{
  *this = edgeModel;
}

EdgeModel *EdgeModel::operator=(const EdgeModel &edgeModel)
{
  if (this != &edgeModel)
  {
    points = edgeModel.points;
    stableEdgels = edgeModel.stableEdgels;
    orientations = edgeModel.orientations;
    normals = edgeModel.normals;
    hasRotationSymmetry = edgeModel.hasRotationSymmetry;
    upStraightDirection = edgeModel.upStraightDirection;
    tableAnchor = edgeModel.tableAnchor;
    Rt_obj2cam = edgeModel.Rt_obj2cam.clone();
  }
  return this;
}

void EdgeModel::generateSilhouettes(const cv::Ptr<const PinholeCamera> &pinholeCamera, int silhouetteCount, std::vector<Silhouette> &silhouettes, float downFactor, int closingIterationsCount) const
{
  EdgeModel canonicalEdgeModel = *this;
  PoseRT model2canonicalPose;
  canonicalEdgeModel.rotateToCanonicalPose(*pinholeCamera, model2canonicalPose);

  silhouettes.clear();
  const float eps = 1e-4;
  CV_Assert(silhouetteCount > 1);
  const int dim = 3;

  for(int i = 0; i < silhouetteCount; ++i)
  {
    for (int j = 0; j < silhouetteCount; ++j)
    {
      if (hasRotationSymmetry && j != 0)
      {
        continue;
      }

      //TODO: generate silhouettes uniformly on the viewing sphere
      double xAngle = i * (2 * CV_PI) / silhouetteCount;
      double yAngle = j * (2 * CV_PI) / silhouetteCount;
      Mat x_rvec_obj = (Mat_<double>(dim, 1) << xAngle, 0.0, 0.0);
      Mat y_rvec_obj = (Mat_<double>(dim, 1) << 0.0, yAngle, 0.0);
      Mat zeroTvec = Mat::zeros(dim, 1, CV_64FC1);

      Mat rvec_obj, tvec_obj;
      composeRT(y_rvec_obj, zeroTvec, x_rvec_obj, zeroTvec, rvec_obj, tvec_obj);

      PoseRT silhouettePose_obj(rvec_obj, tvec_obj);
      PoseRT silhouettePose_cam = silhouettePose_obj.obj2cam(canonicalEdgeModel.Rt_obj2cam);
      silhouettePose_cam = silhouettePose_cam * model2canonicalPose;

      Silhouette currentSilhouette;
      getSilhouette(pinholeCamera, silhouettePose_cam, currentSilhouette, downFactor, closingIterationsCount);
      silhouettes.push_back(currentSilhouette);
    }
  }

#ifdef VISUALIZE_ALL_GENERATED_SILHOUETTES
  for(size_t i = 0; i < silhouettes.size(); ++i)
  {
    Mat image(pinholeCamera->imageSize, CV_8UC1, Scalar(0));
    const int thickness = 3;
    silhouettes[i].draw(image, thickness);
    imshow("silhouette", image);
    waitKey();
  }
#endif
}

static void computeDots(const Mat &mat1, const Mat &mat2, Mat &dst)
{
  Mat m1 = mat1.reshape(1);
  Mat m2 = mat2.reshape(1);
  CV_Assert(m1.size() == m2.size());
  CV_Assert(m1.type() == m2.type());

  Mat products = m1.mul(m2);
  reduce(products, dst, 1, CV_REDUCE_SUM);
}

static void computeNormalDots(const Mat &Rt, const EdgeModel &rotatedEdgeModel, Mat &dots)
{
  Mat R = Rt(Range(0, 3), Range(0, 3));
  Mat t = Rt(Range(0, 3), Range(3, 4));

  Mat vec;
  Mat(t.t() * R).reshape(3).convertTo(vec, CV_32FC1);
  Scalar scalar = Scalar(vec.at<Vec3f>(0));
  Mat edgelsMat = Mat(rotatedEdgeModel.points) + scalar;

  Mat norms;
  computeDots(edgelsMat, edgelsMat, norms);
  sqrt(norms, norms);

  computeDots(edgelsMat, Mat(rotatedEdgeModel.normals), dots);
  float epsf = 1e-4;
  Scalar eps = Scalar::all(epsf);
  dots /= (norms + epsf);
}

void EdgeModel::computeWeights(const PoseRT &pose_cam, cv::Mat &weights) const
{
//  EdgeModel rotatedEdgeModel;
//  rotate_cam(pose_cam, rotatedEdgeModel);

  computeNormalDots(pose_cam.getProjectiveMatrix(), *this, weights);
  Mat expWeights;
  //TODO: move up
  exp(-10.0 * abs(weights), expWeights);
  weights = 2 * expWeights;

  Mat doubleWeights;
  weights.convertTo(doubleWeights, CV_64FC1);
  weights = doubleWeights;
}

void EdgeModel::getSilhouette(const cv::Ptr<const PinholeCamera> &pinholeCamera, const PoseRT &pose_cam, Silhouette &silhouette, float downFactor, int closingIterationsCount) const
{
  silhouette.clear();

  vector<Point2f> projectedPointsVector;
  pinholeCamera->projectPoints(points, pose_cam, projectedPointsVector);

  Mat footprintPoints;
  computeFootprint(projectedPointsVector, pinholeCamera->imageSize, footprintPoints, downFactor, closingIterationsCount);

  silhouette.init(footprintPoints, pose_cam);
}

void EdgeModel::computePointsMask(const std::vector<cv::Point2f> &points, const cv::Size &imageSize, float downFactor, int closingIterationsCount, cv::Mat &image, cv::Point &tl)
{
  CV_Assert(imageSize.height > 0 && imageSize.width > 0);
  bool isValid = false;
  int downRows = static_cast<int>(imageSize.height * downFactor);
  int downCols = static_cast<int>(imageSize.width * downFactor);
  Mat projectedPointsImg = Mat(downRows, downCols, CV_8UC1, Scalar(0));
  tl = Point(downCols, downRows);
  Point br(0, 0);
  for(size_t i=0; i<points.size(); i++)
  {
    Point pt2f = points[i];
    Point downPt = pt2f * downFactor;

    if(!(downPt.x >= 0 && downPt.x < projectedPointsImg.cols && downPt.y >= 0 && downPt.y < projectedPointsImg.rows))
      continue;

    projectedPointsImg.at<uchar>(downPt) = 255;
    isValid = true;

    tl.x = std::min(tl.x, downPt.x);
    tl.y = std::min(tl.y, downPt.y);
    br.x = std::max(br.x, downPt.x);
    br.y = std::max(br.y, downPt.y);
  }
  if(!isValid)
  {
    image = Mat();
    return;
  }

  int elementSize = closingIterationsCount * 2 + 1;

  tl.x = std::max(0, tl.x - elementSize);
  tl.y = std::max(0, tl.y - elementSize);
  br.x = std::min(projectedPointsImg.cols, br.x + elementSize + 1);
  br.y = std::min(projectedPointsImg.rows, br.y + elementSize + 1);

  CV_Assert(tl.x >= 0 && tl.x < projectedPointsImg.cols && tl.y >= 0 && tl.y < projectedPointsImg.rows);
  CV_Assert(br.x > 0 && br.x <= projectedPointsImg.cols && br.y > 0 && br.y <= projectedPointsImg.rows);
  Mat projectedPointsROI = projectedPointsImg(Rect(tl, br));

  //  morphologyEx(projectedPointsROI, projectedPointsROI, MORPH_CLOSE, Mat(), Point(-1, -1), closingIterationsCount );
  Mat structuringElement = getStructuringElement(MORPH_ELLIPSE, Size(elementSize, elementSize), Point(closingIterationsCount, closingIterationsCount));
  morphologyEx(projectedPointsROI, image, MORPH_CLOSE, structuringElement, Point(closingIterationsCount, closingIterationsCount));
}

void EdgeModel::computeFootprint(const std::vector<cv::Point2f> &points, const cv::Size &imageSize, cv::Mat &footprintPoints, float downFactor, int closingIterationsCount)
{
  footprintPoints = Mat();
  Mat projectedViewDependentEdgels;
  vector<Point2f> boundary;

  Mat projectedPointsROI;
  Point tl;
  computePointsMask(points, imageSize, downFactor, closingIterationsCount, projectedPointsROI, tl);
  if (projectedPointsROI.empty())
  {
    return;
  }


#ifdef VISUALIZE_SILHOUETTE_GENERATION
  imshow("projection", projectedPointsROI);
#endif


#ifdef VISUALIZE_SILHOUETTE_GENERATION
  imshow("morphology", projectedPointsROI);
#endif

  vector<vector<Point> > contours;
  findContours(projectedPointsROI, contours, CV_RETR_LIST, CV_CHAIN_APPROX_NONE);
  if(contours.empty())
  {
    return;
  }
#ifdef VISUALIZE_SILHOUETTE_GENERATION
  Mat contoursImage = Mat(projectedPointsROI.size(), CV_8UC1, Scalar(0));
  drawContours(contoursImage, contours, -1, Scalar::all(255) );
  imshow("contours", contoursImage);
  waitKey();
#endif


  boundary.reserve(contours[0].size());
  for(size_t i=0; i<contours.size(); i++)
  {
    std::copy(contours[i].begin(), contours[i].end(), std::back_inserter(boundary));
  }

  projectedViewDependentEdgels = Mat(boundary);

  Point2f tl2f = tl;
  Scalar tlScalar(tl2f.x, tl2f.y);
  projectedViewDependentEdgels = (projectedViewDependentEdgels + tlScalar) / downFactor;

  footprintPoints = projectedViewDependentEdgels.clone();
}

void EdgeModel::rotate_cam(const PoseRT &transformation_cam, EdgeModel &rotatedEdgeModel) const
{
  Mat rvec = transformation_cam.getRvec();
  Mat tvec = transformation_cam.getTvec();

  project3dPoints(points, rvec, tvec, rotatedEdgeModel.points);
  project3dPoints(stableEdgels, rvec, tvec, rotatedEdgeModel.stableEdgels);
  project3dPoints(orientations, rvec, tvec, rotatedEdgeModel.orientations);

  Mat Rt_cam;
  createProjectiveMatrix(rvec, tvec, Rt_cam);

  rotatedEdgeModel.Rt_obj2cam = Rt_cam * Rt_obj2cam;

  transformPoint(Rt_cam, tableAnchor, rotatedEdgeModel.tableAnchor);

  Rt_cam(Range(0, 3), Range(3, 4)).setTo(Scalar(0));
  transformPoint(Rt_cam, upStraightDirection, rotatedEdgeModel.upStraightDirection);
  rotatedEdgeModel.hasRotationSymmetry = hasRotationSymmetry;

  Mat rvec_rot, tvec_rot;
  getRvecTvec(Rt_cam, rvec_rot, tvec_rot);
  project3dPoints(normals, rvec_rot, tvec_rot, rotatedEdgeModel.normals);
}

Mat EdgeModel::rotate_obj(const PoseRT &transformation_obj, EdgeModel &rotatedEdgeModel) const
{
  Mat transformationMatrix;
  getTransformationMatrix(Rt_obj2cam, transformation_obj.getRvec(), transformation_obj.getTvec(), transformationMatrix);

  PoseRT transformation_cam(transformationMatrix);
  rotate_cam(transformation_cam, rotatedEdgeModel);

  return transformationMatrix;
}

void EdgeModel::rotateToCanonicalPose(const PinholeCamera &camera, PoseRT &model2canonicalPose_cam, float distance)
{
  Point3d yAxis(0.0, 1.0, 0.0), zAxis(0.0, 0.0, 1.0);
  PoseRT rotationalExtrinsics;
  rotationalExtrinsics.rvec = camera.extrinsics.getRvec();
  PoseRT invertedRotationalExtrinsics = rotationalExtrinsics.inv();

  Point3d originalYAxis, originalZAxis;
  transformPoint(invertedRotationalExtrinsics.getProjectiveMatrix(), yAxis, originalYAxis);
  transformPoint(invertedRotationalExtrinsics.getProjectiveMatrix(), zAxis, originalZAxis);


  Point3d rotationDir = upStraightDirection.cross(originalYAxis);
//  Point3d rotationDir = upStraightDirection.cross(yAxis);
  Mat rvec_cam;
  point2col(rotationDir, rvec_cam);
  double phi = acos(yAxis.ddot(upStraightDirection) / norm(upStraightDirection));
  rvec_cam = phi * rvec_cam / norm(rvec_cam);

  Mat tvec_cam = Mat::zeros(3, 1, CV_64FC1);
  EdgeModel rotatedEdgeModel;
  rotate_cam(PoseRT(rvec_cam, tvec_cam), rotatedEdgeModel);
  PoseRT firstPose(rvec_cam, tvec_cam);
  model2canonicalPose_cam = firstPose;
  cout << model2canonicalPose_cam << endl;

  *this = rotatedEdgeModel;


  PoseRT invertedExtrinsics = camera.extrinsics.inv();

  Point3d origin(0.0, 0.0, 0.0);
  Point3d originalOrigin;
  transformPoint(invertedExtrinsics.getProjectiveMatrix(), origin, originalOrigin);

  Mat originalOriginMat;
  point2col(originalOrigin, originalOriginMat);

  Mat originalZAxisMat;
  point2col(originalZAxis, originalZAxisMat);
  Mat originalYAxisMat;
  point2col(originalYAxis, originalYAxisMat);



  Mat R, t;
  getRotationTranslation(Rt_obj2cam, R, t);
  tvec_cam = (-t + originalOriginMat);
  tvec_cam += distance * originalZAxisMat;
  rvec_cam = Mat::zeros(3, 1, CV_64FC1);
  PoseRT secondPose(rvec_cam, tvec_cam);
  rotate_cam(secondPose, rotatedEdgeModel);
  model2canonicalPose_cam = secondPose * model2canonicalPose_cam;
  cout << model2canonicalPose_cam << endl;

  *this = rotatedEdgeModel;


//  Mat oldRt_obj2cam = Rt_obj2cam.clone();

  Mat col = Rt_obj2cam(Range(0, 3), Range(2, 3));
  originalZAxisMat.copyTo(col);

  col = Rt_obj2cam(Range(0, 3), Range(1, 2));
  originalYAxisMat.copyTo(col);
  Point3d originalXAsis = originalYAxis.cross(originalZAxis);
  Mat originalXAxisMat;
  point2col(originalXAsis, originalXAxisMat);
  col = Rt_obj2cam(Range(0, 3), Range(0, 1));
  originalXAxisMat.copyTo(col);


  cout << "canonical Rt_obj2cam: " << Rt_obj2cam << endl;

//  Mat thirdRt = Rt_obj2cam * oldRt_obj2cam.inv(DECOMP_SVD);
//  PoseRT thirdPose(thirdRt);
//  model2canonicalPose_cam = thirdPose * model2canonicalPose_cam;
}

cv::Point3f EdgeModel::getObjectCenter() const
{
  Mat R, t;
  getRotationTranslation(Rt_obj2cam, R, t);
  t = t.reshape(3, 1);
  vector<Point3f> pts = t;
  return pts[0];
}

void EdgeModel::clear()
{
  points.clear();
  orientations.clear();
  normals.clear();
  stableEdgels.clear();
  Rt_obj2cam = Mat();
}

void EdgeModel::visualize()
{
  pcl::PointCloud<pcl::PointXYZ>::Ptr pclPoints(new pcl::PointCloud<pcl::PointXYZ>), pclStablePoints(new pcl::PointCloud<pcl::PointXYZ>);
  cv2pcl(points, *pclPoints);
  cv2pcl(stableEdgels, *pclStablePoints);
#ifdef USE_3D_VISUALIZATION
  pcl::visualization::CloudViewer viewer ("all points");
  viewer.showCloud(pclPoints, "points");

  while (!viewer.wasStopped ())
  {
  }

  pcl::visualization::CloudViewer viewer2 ("stable points");
  viewer2.showCloud(pclStablePoints, "stable edgels");
  while (!viewer2.wasStopped ())
  {
  }
#endif
}

void EdgeModel::write(const std::string &filename) const
{
  FileStorage fs(filename, FileStorage::WRITE);
  write(fs);
  fs.release();
}

void EdgeModel::write(cv::FileStorage &fs) const
{
//  fs << "edgeModel" << "{";

  fs << "edgels" << Mat(points);
  fs << "stableEdgels" << Mat(stableEdgels);
  fs << "normals" << Mat(normals);
  fs << "orientations" << Mat(orientations);
  fs << "hasRotationSymmetry" << hasRotationSymmetry;
  fs << "upStraightDirection" << Mat(upStraightDirection);
  fs << "tableAnchor" << Mat(tableAnchor);
  fs << "Rt_obj2cam" << Rt_obj2cam;

//  fs << "}";
}

void EdgeModel::read(const std::string &filename)
{
  FileStorage edgeModelFS(filename, FileStorage::READ);
  if(!edgeModelFS.isOpened())
  {
    CV_Error(CV_StsBadArg, "Cannot open a file " + filename);
  }

  read(edgeModelFS.root());

  edgeModelFS.release();
}

void EdgeModel::read(const cv::FileNode &fn)
{
  Mat edgelsMat;
  fn["edgels"] >> edgelsMat;
  CV_Assert(!edgelsMat.empty());
  points = edgelsMat;

  Mat stableEdgelsMat;
  fn["stableEdgels"] >> stableEdgelsMat;
  if (stableEdgelsMat.empty())
  {
    stableEdgels.clear();
  }
  else
  {
    stableEdgels = stableEdgelsMat;
  }

  Mat normalsMat;
  fn["normals"] >> normalsMat;
  if (normalsMat.empty())
  {
    normals.clear();
  }
  else
  {
    normals = normalsMat;
  }

  Mat orientationsMat;
  fn["orientations"] >> orientationsMat;
  if (orientationsMat.empty())
  {
    orientations.clear();
  }
  else
  {
    orientations = orientationsMat;
  }

  hasRotationSymmetry = static_cast<int>(fn["hasRotationSymmetry"]);

  Mat upStraightDirectionMat;
  fn["upStraightDirection"] >> upStraightDirectionMat;
  CV_Assert(!upStraightDirectionMat.empty());
  upStraightDirection = Point3d(upStraightDirectionMat);

  Mat tableAnchorMat;
  fn["tableAnchor"] >> tableAnchorMat;
  CV_Assert(!tableAnchorMat.empty());
  tableAnchor = Point3d(tableAnchorMat);

  fn["Rt_obj2cam"] >> Rt_obj2cam;
  CV_Assert(!Rt_obj2cam.empty());
}

bool EdgeModel::isAxisCorrect(const std::vector<cv::Point3f> &points, cv::Point3f rotationAxis, int neighborIndex, float distanceFactor, int rotationCount)
{
  if (points.empty())
  {
    return true;
  }

  Scalar centerScalar = mean(points);
  Point3f center(centerScalar[0], centerScalar[1], centerScalar[2]);

  Ptr<DescriptorMatcher> matcher = new FlannBasedMatcher;
  vector<Mat> descriptors;
  descriptors.push_back(Mat(points).reshape(1));
  matcher->add(descriptors);

  vector<vector<DMatch> > selfMatches;
  matcher->knnMatch(Mat(points).reshape(1), selfMatches, neighborIndex + 1);
  vector<float> distances(selfMatches.size());
  for (size_t i = 0; i < selfMatches.size(); ++i)
  {
    distances[i] = selfMatches[i][neighborIndex].distance;
  }
  size_t medianIndex = distances.size() / 2;
  std::nth_element(distances.begin(), distances.begin() + medianIndex,  distances.end());
  float medianDistance = distances[medianIndex];
  float largestDistance = 0.0f;
  for (int rotationIndex = 1; rotationIndex < rotationCount; ++rotationIndex)
  {
    float rotationAngle = rotationIndex * (2.0 * CV_PI / rotationCount);

    const int dim = 3;
    Mat tvec = Mat::zeros(dim, 1, CV_64FC1);
    Mat rvec;
    Point3d rotationAxis_double = rotationAxis;
    point2col(rotationAxis_double, rvec);

    vector<Point3f> rotatedPoints;
    project3dPoints(points, rvec, tvec, rotatedPoints);

    vector<DMatch> matches;
    matcher->match(Mat(rotatedPoints).reshape(1), matches);

    float currentDistance = std::max_element(matches.begin(), matches.end())->distance;
    if (currentDistance > largestDistance)
    {
      largestDistance = currentDistance;
    }
  }

  return (largestDistance < distanceFactor * medianDistance);
}

void computeObjectSystem(const std::vector<cv::Point3f> &points, cv::Mat &Rt_obj2cam)
{
  PCA pca(Mat(points).reshape(1), Mat(), CV_PCA_DATA_AS_ROW);

  Mat R_obj2cam, t_obj2cam;
  pca.eigenvectors.convertTo(R_obj2cam, CV_64FC1);
  pca.mean.convertTo(t_obj2cam, CV_64FC1);
  t_obj2cam = t_obj2cam.t();
  CV_Assert(t_obj2cam.rows == 3 && t_obj2cam.cols == 1);

  createProjectiveMatrix(R_obj2cam, t_obj2cam, Rt_obj2cam);
}
