// redmine usage: This commit refs #388 @2h

// ###############################################################################################
// ###############################################################################################
// ###############################################################################################

/*
 * KITTI_PLAYER v2.
 *
 * Augusto Luis Ballardini, ballardini@disco.unimib.it
 *
 * https://github.com/iralabdisco/kitti_player
 *
 * WARNING: this package is using some C++11
 *
 */


// ###############################################################################################
// ###############################################################################################
// ###############################################################################################

#include <ros/ros.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/locale.hpp>
#include <boost/program_options.hpp>
#include <boost/progress.hpp>
#include <boost/tokenizer.hpp>
#include <boost/tokenizer.hpp>
#include <cv_bridge/cv_bridge.h>
#include <fstream>
#include <image_transport/image_transport.h>
#include <iostream>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl_ros/point_cloud.h>
#include <pcl/point_types.h>
#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/NavSatFix.h>
#include <sensor_msgs/PointCloud2.h>
#include <sstream>
#include <string>
#include <tf/LinearMath/Transform.h>
#include <tf/transform_broadcaster.h>
#include <tf/transform_listener.h>
#include <time.h>

using namespace std;
using namespace pcl;
using namespace ros;
using namespace tf;

namespace po = boost::program_options;

struct kitti_player_options
{
    string  path;
    float   frequency;      // publisher frequency. 1 > Kitti default 10Hz
    bool    all_data;       // publish everything
    bool    velodyne;       // publish velodyne point clouds /as PCL
    bool    gps;            // publish GPS sensor_msgs/NavSatFix    message
    bool    imu;            // publish IMU sensor_msgs/Imu Message  message
    bool    grayscale;      // publish
    bool    color;          // publish
    bool    viewer;         // enable CV viewer
    bool    timestamps;     // use KITTI timestamps;
    bool    sendTransform;  // publish velodyne TF IMU 3DOF orientation wrt fixed frame
};

int publish_velodyne(ros::Publisher &pub, string infile, std_msgs::Header *header,bool sendtransform)
{
    fstream input(infile.c_str(), ios::in | ios::binary);
    if(!input.good())
    {
        ROS_ERROR_STREAM ( "Could not read file: " << infile );
        return 0;
    }
    else
    {
        ROS_DEBUG_STREAM ("reading " << infile);
        input.seekg(0, ios::beg);

        pcl::PointCloud<pcl::PointXYZI>::Ptr points (new pcl::PointCloud<pcl::PointXYZI>);

        int i;
        for (i=0; input.good() && !input.eof(); i++) {
            pcl::PointXYZI point;
            input.read((char *) &point.x, 3*sizeof(float));
            input.read((char *) &point.intensity, sizeof(float));
            points->push_back(point);
        }
        input.close();

        //workaround for the PCL headers... http://wiki.ros.org/hydro/Migration#PCL
        sensor_msgs::PointCloud2 pc2;

        if (sendtransform)
            pc2.header.frame_id= "imu" ;
        else
            pc2.header.frame_id= ros::this_node::getName();
        pc2.header.stamp=header->stamp;
        points->header = pcl_conversions::toPCL(pc2.header);
        pub.publish(points);

        return 1;
    }
}

int getCalibration(string dir_root, string camera_name, double* K,std::vector<double> & D,double *R,double* P){
/*
 *   from: http://kitti.is.tue.mpg.de/kitti/devkit_raw_data.zip
 *   calib_cam_to_cam.txt: Camera-to-camera calibration
 *   --------------------------------------------------
 *
 *     - S_xx: 1x2 size of image xx before rectification
 *     - K_xx: 3x3 calibration matrix of camera xx before rectification
 *     - D_xx: 1x5 distortion vector of camera xx before rectification
 *     - R_xx: 3x3 rotation matrix of camera xx (extrinsic)
 *     - T_xx: 3x1 translation vector of camera xx (extrinsic)
 *     - S_rect_xx: 1x2 size of image xx after rectification
 *     - R_rect_xx: 3x3 rectifying rotation to make image planes co-planar
 *     - P_rect_xx: 3x4 projection matrix after rectification
*/

    //    double K[9];         // Calibration Matrix
    //    double D[5];         // Distortion Coefficients
    //    double R[9];         // Rectification Matrix
    //    double P[12];        // Projection Matrix Rectified (u,v,w) = P * R * (x,y,z,q)

    string calib_cam_to_cam=dir_root+"calib_cam_to_cam.txt";
    ifstream file_c2c(calib_cam_to_cam.c_str());
    if (!file_c2c.is_open())
        return false;

    ROS_INFO_STREAM("Reading camera" << camera_name << " calibration from " << calib_cam_to_cam);

    typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
    boost::char_separator<char> sep{" "};

    vector< string > vec;
    string line="";
    char index=0;
    tokenizer::iterator token_iterator;

    while (getline(file_c2c,line))
    {
        // Parse string phase 1, tokenize it using Boost.
        tokenizer tok(line,sep);

        // Move the iterator at the beginning of the tokenize vector and check for K/D/R/P matrices.

        token_iterator=tok.begin();
        if (strcmp((*token_iterator).c_str(),((string)(string("K_")+camera_name+string(":"))).c_str())==0) //Calibration Matrix
        {
            index=0; //should be 9 at the end
            ROS_DEBUG_STREAM("K_" << camera_name);
            for (token_iterator++; token_iterator != tok.end(); token_iterator++)
            {
//                std::cout << *token_iterator << '\n';
                K[index++]=boost::lexical_cast<double>(*token_iterator);
            }
        }

        token_iterator=tok.begin();
        if (strcmp((*token_iterator).c_str(),((string)(string("D_")+camera_name+string(":"))).c_str())==0) //Distortion Coefficients
        {
            index=0; //should be 5 at the end
            ROS_DEBUG_STREAM("D_" << camera_name);
            for (token_iterator++; token_iterator != tok.end(); token_iterator++)
            {
//                std::cout << *token_iterator << '\n';
                D[index++]=boost::lexical_cast<double>(*token_iterator);
            }
        }

        token_iterator=tok.begin();
        if (strcmp((*token_iterator).c_str(),((string)(string("R_")+camera_name+string(":"))).c_str())==0) //Rectification Matrix
        {
            index=0; //should be 12 at the end
            ROS_DEBUG_STREAM("R_" << camera_name);
            for (token_iterator++; token_iterator != tok.end(); token_iterator++)
            {
//                std::cout << *token_iterator << '\n';
                R[index++]=boost::lexical_cast<double>(*token_iterator);
            }
        }

        token_iterator=tok.begin();
        if (strcmp((*token_iterator).c_str(),((string)(string("P_rect_")+camera_name+string(":"))).c_str())==0) //Projection Matrix Rectified
        {
            index=0; //should be 12 at the end
            ROS_DEBUG_STREAM("P_rect_" << camera_name);
            for (token_iterator++; token_iterator != tok.end(); token_iterator++)
            {
//                std::cout << *token_iterator << '\n';
                P[index++]=boost::lexical_cast<double>(*token_iterator);
            }
        }

    }
    ROS_INFO_STREAM("... ok");
    return true;
}

int getGPS(string filename, sensor_msgs::NavSatFix *ros_msgGpsFix, std_msgs::Header *header)
{
    ifstream file_oxts(filename.c_str());
    if (!file_oxts.is_open()){
        ROS_ERROR_STREAM("Fail to open " << filename);
        return 0;
    }

    ROS_DEBUG_STREAM("Reading GPS data from oxts file: " << filename );

    typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
    boost::char_separator<char> sep{" "};

    string line="";

    getline(file_oxts,line);
    tokenizer tok(line,sep);
    vector<string> s(tok.begin(), tok.end());

    ros_msgGpsFix->header.frame_id = ros::this_node::getName();
    ros_msgGpsFix->header.stamp = header->stamp;
    ros_msgGpsFix->header.seq = 0;

    ros_msgGpsFix->latitude  = boost::lexical_cast<double>(s[0]);
    ros_msgGpsFix->longitude = boost::lexical_cast<double>(s[1]);
    ros_msgGpsFix->altitude  = boost::lexical_cast<double>(s[2]);

    ros_msgGpsFix->position_covariance_type = sensor_msgs::NavSatFix::COVARIANCE_TYPE_APPROXIMATED;
    for (int i=0;i<9;i++)
        ros_msgGpsFix->position_covariance[i] = 0.0f;

    ros_msgGpsFix->position_covariance[0] = boost::lexical_cast<double>(s[23]);
    ros_msgGpsFix->position_covariance[4] = boost::lexical_cast<double>(s[23]);
    ros_msgGpsFix->position_covariance[8] = boost::lexical_cast<double>(s[23]);

    ros_msgGpsFix->status.service = sensor_msgs::NavSatStatus::SERVICE_GPS;
    ros_msgGpsFix->status.status  = sensor_msgs::NavSatStatus::STATUS_GBAS_FIX;

    return 1;
}

int getIMU(string filename, sensor_msgs::Imu *ros_msgImu, std_msgs::Header *header)
{
    ifstream file_oxts(filename.c_str());
    if (!file_oxts.is_open())
    {
        ROS_ERROR_STREAM("Fail to open " << filename);
        return 0;
    }

    ROS_DEBUG_STREAM("Reading IMU data from oxts file: " << filename );

    typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
    boost::char_separator<char> sep{" "};

    string line="";

    getline(file_oxts,line);
    tokenizer tok(line,sep);
    vector<string> s(tok.begin(), tok.end());

    ros_msgImu->header.frame_id = ros::this_node::getName();
    ros_msgImu->header.stamp = header->stamp;
    ros_msgImu->header.seq = 0;

    //    - ax:      acceleration in x, i.e. in direction of vehicle front (m/s^2)
    //    - ay:      acceleration in y, i.e. in direction of vehicle left (m/s^2)
    //    - az:      acceleration in z, i.e. in direction of vehicle top (m/s^2)
    ros_msgImu->linear_acceleration.x = boost::lexical_cast<double>(s[11]);
    ros_msgImu->linear_acceleration.y = boost::lexical_cast<double>(s[12]);
    ros_msgImu->linear_acceleration.z = boost::lexical_cast<double>(s[13]);

    //    - vf:      forward velocity, i.e. parallel to earth-surface (m/s)
    //    - vl:      leftward velocity, i.e. parallel to earth-surface (m/s)
    //    - vu:      upward velocity, i.e. perpendicular to earth-surface (m/s)
    ros_msgImu->angular_velocity.x = boost::lexical_cast<double>(s[8]);
    ros_msgImu->angular_velocity.y = boost::lexical_cast<double>(s[9]);
    ros_msgImu->angular_velocity.z = boost::lexical_cast<double>(s[10]);

    //    - roll:    roll angle (rad),  0 = level, positive = left side up (-pi..pi)
    //    - pitch:   pitch angle (rad), 0 = level, positive = front down (-pi/2..pi/2)
    //    - yaw:     heading (rad),     0 = east,  positive = counter clockwise (-pi..pi)
    tf::Quaternion q=tf::createQuaternionFromRPY(   boost::lexical_cast<double>(s[3]),
                                                    boost::lexical_cast<double>(s[4]),
                                                    boost::lexical_cast<double>(s[5])
                                                    );
    ros_msgImu->orientation.x = q.getX();
    ros_msgImu->orientation.y = q.getY();
    ros_msgImu->orientation.z = q.getZ();
    ros_msgImu->orientation.w = q.getW();

    return 1;
}

std_msgs::Header parseTime(string timestamp)
{
    //Epoch time conversion
    //http://www.epochconverter.com/programming/functions-c.php

    std_msgs::Header header;

    typedef boost::tokenizer<boost::char_separator<char> > tokenizer;

    // example: 2011-09-26 13:21:35.134391552
    //          01234567891111111111222222222
    //                    0123456789012345678
    struct tm t = {0};  // Initalize to all 0's
    t.tm_year = boost::lexical_cast<int>(timestamp.substr(0,4)) - 1900;
    t.tm_mon  = boost::lexical_cast<int>(timestamp.substr(5,2));
    t.tm_mday = boost::lexical_cast<int>(timestamp.substr(8,2));
    t.tm_hour = boost::lexical_cast<int>(timestamp.substr(11,2));
    t.tm_min  = boost::lexical_cast<int>(timestamp.substr(14,2));
    t.tm_sec  = boost::lexical_cast<int>(timestamp.substr(17,2));
    t.tm_isdst = -1;
    time_t timeSinceEpoch = mktime(&t);

    header.stamp.sec  = timeSinceEpoch;
    header.stamp.nsec = boost::lexical_cast<int>(timestamp.substr(20,8));

    return header;
}


int main(int argc, char **argv)
{
    kitti_player_options options;
    po::variables_map vm;

    po::options_description desc("Kitti_player, a player for KITTI raw datasets\nDatasets can be downloaded from: http://www.cvlibs.net/datasets/kitti/raw_data.php\n\nAllowed options",200);
    desc.add_options()
        ("help,h"                                                                                          ,  "help message")
        ("directory ,d",  po::value<string>(&options.path)->required()                                     ,  "*required* - path to the kitti dataset Directory")
        ("frequency ,f",  po::value<float>(&options.frequency)     ->default_value(1.0)                    ,  "set replay Frequency")
        ("all       ,a",  po::value<bool> (&options.all_data)      ->implicit_value(1) ->default_value(0)  ,  "replay All data")
        ("velodyne  ,v",  po::value<bool> (&options.velodyne)      ->implicit_value(1) ->default_value(0)  ,  "replay Velodyne data")
        ("gps       ,g",  po::value<bool> (&options.gps)           ->implicit_value(1) ->default_value(0)  ,  "replay Gps data")
        ("imu       ,i",  po::value<bool> (&options.imu)           ->implicit_value(1) ->default_value(0)  ,  "replay Imu data")
        ("grayscale ,G",  po::value<bool> (&options.grayscale)     ->implicit_value(1) ->default_value(0)  ,  "replay Stereo Grayscale images")
        ("color     ,C",  po::value<bool> (&options.color)         ->implicit_value(1) ->default_value(0)  ,  "replay Stereo Color images")
        ("viewer      ",  po::value<bool> (&options.viewer)        ->implicit_value(1) ->default_value(0)  ,  "enable image viewer")
        ("timestamps,T",  po::value<bool> (&options.timestamps)    ->implicit_value(1) ->default_value(0)  ,  "use KITTI timestamps")
        ("sendtransform", po::value<bool> (&options.sendTransform) ->implicit_value(1) ->default_value(0)  , "publish velodyne TF IMU 3DOF orientation wrt fixed frame")

    ;

    try // parse options
    {
        po::parsed_options parsed = po::command_line_parser(argc, argv).options(desc).allow_unregistered().run();
        po::store(parsed, vm);
        po::notify(vm);

        vector<string> to_pass_further = po::collect_unrecognized(parsed.options, po::include_positional);

//        if (to_pass_further.size()>0)
//        {
//            ROS_WARN_STREAM("Unknown Options Detected, shutting down node\n");
//            cerr << desc << endl;
//            return 1;
//        }
    }
    catch(...)
    {
        cerr << desc << endl;

        cout << "kitti_player needs a directory tree like the following:" << endl;
        cout << "└── 2011_09_26_drive_0001_sync" << endl;
        cout << "    ├── image_00              " << endl;
        cout << "    │   └── data              " << endl;
        cout << "    │   └ timestamps.txt      " << endl;
        cout << "    ├── image_01              " << endl;
        cout << "    │   └── data              " << endl;
        cout << "    │   └ timestamps.txt      " << endl;
        cout << "    ├── image_02              " << endl;
        cout << "    │   └── data              " << endl;
        cout << "    │   └ timestamps.txt      " << endl;
        cout << "    ├── image_03              " << endl;
        cout << "    │   └── data              " << endl;
        cout << "    │   └ timestamps.txt      " << endl;
        cout << "    ├── oxts                  " << endl;
        cout << "    │   └── data              " << endl;
        cout << "    │   └ timestamps.txt      " << endl;
        cout << "    ├── velodyne_points       " << endl;
        cout << "    │   └── data              " << endl;
        cout << "    │     └ timestamps.txt    " << endl;
        cout << "    └── calib_cam_to_cam.txt  " << endl << endl;

        ROS_WARN_STREAM("Parse error, shutting down node\n");
        return 1;
    }

    ros::init(argc, argv, "kitti_player");
    ros::NodeHandle node("kitti_player");
    ros::Rate loop_rate(options.frequency);

    DIR *dir;
    struct dirent *ent;
    unsigned long total_entries = 0;        //number of elements to be played
    unsigned long entries_played  = 0;      //number of elements played until now
    unsigned int len = 0;                   //counting elements support variable
    string dir_root             ;
    string dir_image00          ;string full_filename_image00;   string dir_timestamp_image00;
    string dir_image01          ;string full_filename_image01;   string dir_timestamp_image01;
    string dir_image02          ;string full_filename_image02;   string dir_timestamp_image02;
    string dir_image03          ;string full_filename_image03;   string dir_timestamp_image03;
    string dir_oxts             ;string full_filename_oxts;      string dir_timestamp_oxts;
    string dir_velodyne_points  ;string full_filename_velodyne;  string dir_timestamp_velodyne; //average of start&end (time of scan)
    string str_support;
    cv::Mat cv_image00;
    cv::Mat cv_image01;
    cv::Mat cv_image02;
    cv::Mat cv_image03;
    std_msgs::Header header_support;

    image_transport::ImageTransport it(node);
    image_transport::CameraPublisher pub00 = it.advertiseCamera("grayscale/left/image_raw", 1);
    image_transport::CameraPublisher pub01 = it.advertiseCamera("grayscale/right/image_raw", 1);
    image_transport::CameraPublisher pub02 = it.advertiseCamera("color/left/image_raw", 1);
    image_transport::CameraPublisher pub03 = it.advertiseCamera("color/right/image_raw", 1);

    sensor_msgs::Image ros_msg00;
    sensor_msgs::Image ros_msg01;
    sensor_msgs::Image ros_msg02;
    sensor_msgs::Image ros_msg03;

//    sensor_msgs::CameraInfo ros_cameraInfoMsg;
    sensor_msgs::CameraInfo ros_cameraInfoMsg_camera00;
    sensor_msgs::CameraInfo ros_cameraInfoMsg_camera01;
    sensor_msgs::CameraInfo ros_cameraInfoMsg_camera02;
    sensor_msgs::CameraInfo ros_cameraInfoMsg_camera03;

    cv_bridge::CvImage cv_bridge_img;

    ros::Publisher map_pub = node.advertise<pcl::PointCloud<pcl::PointXYZ> > ("hdl64e", 1, true);
    ros::Publisher gps_pub = node.advertise<sensor_msgs::NavSatFix>  ("oxts/gps", 1, true);
    ros::Publisher imu_pub = node.advertise<sensor_msgs::Imu>  ("oxts/imu", 1, true);

    sensor_msgs::NavSatFix ros_msgGpsFix;
    sensor_msgs::Imu ros_msgImu;

    static tf::TransformBroadcaster br;
    tf::Transform transform;


    if (vm.count("help")) {
        cout << desc << endl;

        cout << "kitti_player needs a directory tree like the following:" << endl;
        cout << "└── 2011_09_26_drive_0001_sync" << endl;
        cout << "    ├── image_00              " << endl;
        cout << "    │   └── data              " << endl;
        cout << "    │   └ timestamps.txt      " << endl;
        cout << "    ├── image_01              " << endl;
        cout << "    │   └── data              " << endl;
        cout << "    │   └ timestamps.txt      " << endl;
        cout << "    ├── image_02              " << endl;
        cout << "    │   └── data              " << endl;
        cout << "    │   └ timestamps.txt      " << endl;
        cout << "    ├── image_03              " << endl;
        cout << "    │   └── data              " << endl;
        cout << "    │   └ timestamps.txt      " << endl;
        cout << "    ├── oxts                  " << endl;
        cout << "    │   └── data              " << endl;
        cout << "    │   └ timestamps.txt      " << endl;
        cout << "    ├── velodyne_points       " << endl;
        cout << "    │   └── data              " << endl;
        cout << "    │     └ timestamps.txt    " << endl;
        cout << "    └── calib_cam_to_cam.txt  " << endl << endl;

        return 1;
    }

    if (!(options.all_data || options.color || options.gps || options.grayscale || options.imu || options.velodyne))
    {
        ROS_WARN_STREAM("Job finished without playing the dataset. No 'publishing' parameters provided");
        return 1;
    }

    dir_root             = options.path;
    dir_image00          = options.path;
    dir_image01          = options.path;
    dir_image02          = options.path;
    dir_image03          = options.path;
    dir_oxts             = options.path;
    dir_velodyne_points  = options.path;
    (*(options.path.end()-1) != '/' ? dir_root            = options.path+"/"                      : dir_root            = options.path);
    (*(options.path.end()-1) != '/' ? dir_image00         = options.path+"/image_00/data/"        : dir_image00         = options.path+"image_00/data/");
    (*(options.path.end()-1) != '/' ? dir_image01         = options.path+"/image_01/data/"        : dir_image01         = options.path+"image_01/data/");
    (*(options.path.end()-1) != '/' ? dir_image02         = options.path+"/image_02/data/"        : dir_image02         = options.path+"image_02/data/");
    (*(options.path.end()-1) != '/' ? dir_image03         = options.path+"/image_03/data/"        : dir_image03         = options.path+"image_03/data/");
    (*(options.path.end()-1) != '/' ? dir_oxts            = options.path+"/oxts/data/"            : dir_oxts            = options.path+"oxts/data/");
    (*(options.path.end()-1) != '/' ? dir_velodyne_points = options.path+"/velodyne_points/data/" : dir_velodyne_points = options.path+"velodyne_points/data/");

    (*(options.path.end()-1) != '/' ? dir_timestamp_image00    = options.path+"/image_00/"            : dir_timestamp_image00   = options.path+"image_00/");
    (*(options.path.end()-1) != '/' ? dir_timestamp_image01    = options.path+"/image_01/"            : dir_timestamp_image01   = options.path+"image_01/");
    (*(options.path.end()-1) != '/' ? dir_timestamp_image02    = options.path+"/image_02/"            : dir_timestamp_image02   = options.path+"image_02/");
    (*(options.path.end()-1) != '/' ? dir_timestamp_image03    = options.path+"/image_03/"            : dir_timestamp_image03   = options.path+"image_03/");
    (*(options.path.end()-1) != '/' ? dir_timestamp_oxts       = options.path+"/oxts/"                : dir_timestamp_oxts      = options.path+"oxts/");
    (*(options.path.end()-1) != '/' ? dir_timestamp_velodyne   = options.path+"/velodyne_points/"     : dir_timestamp_velodyne  = options.path+"velodyne_points/");


    // Check all the directories
    if (
            (options.all_data   && (   (opendir(dir_image00.c_str())            == NULL) ||
                                       (opendir(dir_image01.c_str())            == NULL) ||
                                       (opendir(dir_image02.c_str())            == NULL) ||
                                       (opendir(dir_image03.c_str())            == NULL) ||
                                       (opendir(dir_oxts.c_str())               == NULL) ||
                                       (opendir(dir_velodyne_points.c_str())    == NULL)))
            ||
            (options.color      && (   (opendir(dir_image02.c_str())            == NULL) ||
                                       (opendir(dir_image03.c_str())            == NULL)))
            ||
            (options.grayscale  && (   (opendir(dir_image00.c_str())            == NULL) ||
                                       (opendir(dir_image01.c_str())            == NULL)))
            ||
            (options.imu        && (   (opendir(dir_oxts.c_str())               == NULL)))
            ||
            (options.gps        && (   (opendir(dir_oxts.c_str())               == NULL)))
            ||
            (options.velodyne   && (   (opendir(dir_velodyne_points.c_str())    == NULL)))
            ||
            (options.timestamps && (   (opendir(dir_timestamp_image00.c_str())      == NULL) ||
                                       (opendir(dir_timestamp_image01.c_str())      == NULL) ||
                                       (opendir(dir_timestamp_image02.c_str())      == NULL) ||
                                       (opendir(dir_timestamp_image03.c_str())      == NULL) ||
                                       (opendir(dir_timestamp_oxts.c_str())         == NULL) ||
                                       (opendir(dir_timestamp_velodyne.c_str())     == NULL)))

        )
    {
        ROS_ERROR("Incorrect tree directory , use --help for details");
        return 1;
    }
    else
    {
        ROS_INFO_STREAM ("Checking directories...");
        ROS_INFO_STREAM (options.path << "\t[OK]");
    }

    //count elements in the folder

    if (options.all_data)
    {
        dir = opendir(dir_image02.c_str());
        while(ent = readdir(dir))
        {
            //skip . & ..
            len = strlen (ent->d_name);
            //skip . & ..
            if (len>2)
                total_entries++;
        }
        closedir (dir);
    }
    else
    {
        bool done=false;
        if (!done && options.color)
        {
            total_entries=0;
            dir = opendir(dir_image02.c_str());
            while(ent = readdir(dir))
            {
                //skip . & ..
                len = strlen (ent->d_name);
                //skip . & ..
                if (len>2)
                    total_entries++;
            }            closedir (dir);
            done=true;
        }
        if (!done && options.grayscale)
        {
            total_entries=0;
            dir = opendir(dir_image00.c_str());
            while(ent = readdir(dir))
            {
                //skip . & ..
                len = strlen (ent->d_name);
                //skip . & ..
                if (len>2)
                    total_entries++;
            }
            closedir (dir);
            done=true;
        }
        if (!done && options.gps)
        {
            total_entries=0;
            dir = opendir(dir_oxts.c_str());
            while(ent = readdir(dir))
            {
                //skip . & ..
                len = strlen (ent->d_name);
                //skip . & ..
                if (len>2)
                    total_entries++;
            }
            closedir (dir);
            done=true;
        }
        if (!done && options.imu)
        {
            total_entries=0;
            dir = opendir(dir_oxts.c_str());
            while(ent = readdir(dir))
            {
                //skip . & ..
                len = strlen (ent->d_name);
                //skip . & ..
                if (len>2)
                    total_entries++;
            }
            closedir (dir);
            done=true;
        }
        if (!done && options.velodyne)
        {
            total_entries=0;
            dir = opendir(dir_oxts.c_str());            
            while(ent = readdir(dir))
            {
                //skip . & ..
                len = strlen (ent->d_name);
                //skip . & ..
                if (len>2)
                    total_entries++;
            }
            closedir (dir);
            done=true;
        }
    }

    if(options.viewer)
    {
        ROS_INFO_STREAM("Opening CV viewer(s)");
        if(options.color || options.all_data)
        {
            ROS_DEBUG_STREAM("color||all " << options.color << " " << options.all_data);
            cv::namedWindow("CameraSimulator Color Viewer",CV_WINDOW_AUTOSIZE);
            full_filename_image02 = dir_image02 + boost::str(boost::format("%010d") % 0 ) + ".png";
            cv_image02 = cv::imread(full_filename_image02, CV_LOAD_IMAGE_UNCHANGED);
            cv::waitKey(5);
        }
        if(options.grayscale || options.all_data)
        {
            ROS_DEBUG_STREAM("grayscale||all " << options.grayscale << " " << options.all_data);
            cv::namedWindow("CameraSimulator Grayscale Viewer",CV_WINDOW_AUTOSIZE);
            full_filename_image00 = dir_image00 + boost::str(boost::format("%010d") % 0 ) + ".png";
            cv_image00 = cv::imread(full_filename_image00, CV_LOAD_IMAGE_UNCHANGED);
            cv::waitKey(5);
        }
        ROS_INFO_STREAM("Opening CV viewer(s)... OK");
    }

    // CAMERA INFO SECTION: read one for all

    ros_cameraInfoMsg_camera00.header.seq = 0;
    ros_cameraInfoMsg_camera00.header.stamp = ros::Time::now();
    ros_cameraInfoMsg_camera00.header.frame_id = ros::this_node::getName();
    ros_cameraInfoMsg_camera00.height = 0;
    ros_cameraInfoMsg_camera00.width  = 0;
    ros_cameraInfoMsg_camera00.D.resize(5);

    ros_cameraInfoMsg_camera01.header.seq = 0;
    ros_cameraInfoMsg_camera01.header.stamp = ros::Time::now();
    ros_cameraInfoMsg_camera01.header.frame_id = ros::this_node::getName();
    ros_cameraInfoMsg_camera01.height = 0;
    ros_cameraInfoMsg_camera01.width  = 0;
    ros_cameraInfoMsg_camera01.D.resize(5);

    ros_cameraInfoMsg_camera02.header.seq = 0;
    ros_cameraInfoMsg_camera02.header.stamp = ros::Time::now();
    ros_cameraInfoMsg_camera02.header.frame_id = ros::this_node::getName();
    ros_cameraInfoMsg_camera02.height = 0;
    ros_cameraInfoMsg_camera02.width  = 0;
    ros_cameraInfoMsg_camera02.D.resize(5);

    ros_cameraInfoMsg_camera03.header.seq = 0;
    ros_cameraInfoMsg_camera03.header.stamp = ros::Time::now();
    ros_cameraInfoMsg_camera03.header.frame_id = ros::this_node::getName();
    ros_cameraInfoMsg_camera03.height = 0;
    ros_cameraInfoMsg_camera03.width  = 0;
    ros_cameraInfoMsg_camera03.D.resize(5);

    if(options.color || options.all_data)
    {
        if(
           !(getCalibration(dir_root,"02",ros_cameraInfoMsg_camera02.K.data(),ros_cameraInfoMsg_camera02.D,ros_cameraInfoMsg_camera02.R.data(),ros_cameraInfoMsg_camera02.P.data()) &&
           getCalibration(dir_root,"03",ros_cameraInfoMsg_camera03.K.data(),ros_cameraInfoMsg_camera03.D,ros_cameraInfoMsg_camera03.R.data(),ros_cameraInfoMsg_camera03.P.data()))
          )
        {
            ROS_ERROR_STREAM("Error reading CAMERA02/CAMERA03 calibration");
            node.shutdown();
            return 1;
        }
        //Assume same height/width for the camera pair
        full_filename_image02 = dir_image02 + boost::str(boost::format("%010d") % 0 ) + ".png";
        cv_image02 = cv::imread(full_filename_image02, CV_LOAD_IMAGE_UNCHANGED);
        cv::waitKey(5);
        ros_cameraInfoMsg_camera03.height = ros_cameraInfoMsg_camera02.height = cv_image02.rows;
        ros_cameraInfoMsg_camera03.width  = ros_cameraInfoMsg_camera02.width  = cv_image02.cols;
    }

    if(options.grayscale || options.all_data)
    {
        if(
           !(getCalibration(dir_root,"00",ros_cameraInfoMsg_camera00.K.data(),ros_cameraInfoMsg_camera00.D,ros_cameraInfoMsg_camera00.R.data(),ros_cameraInfoMsg_camera00.P.data()) &&
           getCalibration(dir_root,"01",ros_cameraInfoMsg_camera01.K.data(),ros_cameraInfoMsg_camera01.D,ros_cameraInfoMsg_camera01.R.data(),ros_cameraInfoMsg_camera01.P.data()))
          )
        {
            ROS_ERROR_STREAM("Error reading CAMERA00/CAMERA01 calibration");
            node.shutdown();
            return 1;
        }
        //Assume same height/width for the camera pair
        full_filename_image00 = dir_image00 + boost::str(boost::format("%010d") % 0 ) + ".png";
        cv_image00 = cv::imread(full_filename_image00, CV_LOAD_IMAGE_UNCHANGED);
        cv::waitKey(5);
        ros_cameraInfoMsg_camera01.height = ros_cameraInfoMsg_camera00.height = cv_image00.rows;
        ros_cameraInfoMsg_camera01.width  = ros_cameraInfoMsg_camera00.width  = cv_image00.cols;
    }

    boost::progress_display progress(total_entries) ;

    do
    {
        if(options.color || options.all_data)
        {
            full_filename_image02 = dir_image02 + boost::str(boost::format("%010d") % entries_played ) + ".png";
            full_filename_image03 = dir_image03 + boost::str(boost::format("%010d") % entries_played ) + ".png";
            ROS_DEBUG_STREAM ( full_filename_image02 << endl << full_filename_image03 << endl << endl);

            cv_image02 = cv::imread(full_filename_image02, CV_LOAD_IMAGE_UNCHANGED);
            cv_image03 = cv::imread(full_filename_image03, CV_LOAD_IMAGE_UNCHANGED);

            if ( (cv_image02.data == NULL) || (cv_image03.data == NULL) ){
                ROS_ERROR_STREAM("Error reading color images (02 & 03)");
                ROS_ERROR_STREAM(full_filename_image02 << endl << full_filename_image03);
                node.shutdown();
                return 1;
            }

            if(options.viewer)
            {
                //display the left image only
                cv::imshow("CameraSimulator Color Viewer",cv_image02);
                //give some time to draw images
                cv::waitKey(5);
            }

            cv_bridge_img.encoding = sensor_msgs::image_encodings::BGR8; //sensor_msgs::image_encodings::MONO8
            cv_bridge_img.header.frame_id = ros::this_node::getName();
            cv_bridge_img.header.seq = entries_played;

            if (!options.timestamps)
            {
                cv_bridge_img.header.stamp = ros::Time::now();
                ros_cameraInfoMsg_camera02.header.stamp = cv_bridge_img.header.stamp;
            }
            else
            {

                str_support = dir_timestamp_image02 + "timestamps.txt";
                ifstream timestamps(str_support.c_str());
                if (!timestamps.is_open())
                {
                    ROS_ERROR_STREAM("Fail to open " << timestamps);
                    return 1;
                }
                timestamps.seekg(30*entries_played);
                getline(timestamps,str_support);
                cv_bridge_img.header.stamp = parseTime(str_support).stamp;
                ros_cameraInfoMsg_camera02.header.stamp = cv_bridge_img.header.stamp;
            }
            cv_bridge_img.image = cv_image02;
            cv_bridge_img.toImageMsg(ros_msg02);

            if (!options.timestamps)
            {
                cv_bridge_img.header.stamp = ros::Time::now();
                ros_cameraInfoMsg_camera03.header.stamp = cv_bridge_img.header.stamp;
            }
            else
            {

                str_support = dir_timestamp_image03 + "timestamps.txt";
                ifstream timestamps(str_support.c_str());
                if (!timestamps.is_open())
                {
                    ROS_ERROR_STREAM("Fail to open " << timestamps);
                    return 1;
                }
                timestamps.seekg(30*entries_played);
                getline(timestamps,str_support);
                cv_bridge_img.header.stamp = parseTime(str_support).stamp;
                ros_cameraInfoMsg_camera03.header.stamp = cv_bridge_img.header.stamp;
            }

            cv_bridge_img.image = cv_image03;
            cv_bridge_img.toImageMsg(ros_msg03);

            pub02.publish(ros_msg02,ros_cameraInfoMsg_camera02);
            pub03.publish(ros_msg03,ros_cameraInfoMsg_camera03);

        }

        if(options.grayscale || options.all_data)
        {
            full_filename_image00 = dir_image00 + boost::str(boost::format("%010d") % entries_played ) + ".png";
            full_filename_image01 = dir_image01 + boost::str(boost::format("%010d") % entries_played ) + ".png";
            ROS_DEBUG_STREAM ( full_filename_image00 << endl << full_filename_image01 << endl << endl);

            cv_image00 = cv::imread(full_filename_image00, CV_LOAD_IMAGE_UNCHANGED);
            cv_image01 = cv::imread(full_filename_image01, CV_LOAD_IMAGE_UNCHANGED);

            if ( (cv_image00.data == NULL) || (cv_image01.data == NULL) ){
                ROS_ERROR_STREAM("Error reading color images (00 & 01)");
                ROS_ERROR_STREAM(full_filename_image00 << endl << full_filename_image01);
                node.shutdown();
                return 1;
            }

            if(options.viewer)
            {
                //display the left image only
                cv::imshow("CameraSimulator Grayscale Viewer",cv_image00);
                //give some time to draw images
                cv::waitKey(5);
            }

            cv_bridge_img.encoding = sensor_msgs::image_encodings::MONO8;
            cv_bridge_img.header.frame_id = ros::this_node::getName();
            cv_bridge_img.header.seq = entries_played;

            if (!options.timestamps)
            {
                cv_bridge_img.header.stamp = ros::Time::now();
                ros_cameraInfoMsg_camera00.header.stamp = cv_bridge_img.header.stamp;
            }
            else
            {

                str_support = dir_timestamp_image02 + "timestamps.txt";
                ifstream timestamps(str_support.c_str());
                if (!timestamps.is_open())
                {
                    ROS_ERROR_STREAM("Fail to open " << timestamps);
                    return 1;
                }
                timestamps.seekg(30*entries_played);
                getline(timestamps,str_support);
                cv_bridge_img.header.stamp = parseTime(str_support).stamp;
                ros_cameraInfoMsg_camera00.header.stamp = cv_bridge_img.header.stamp;
            }
            cv_bridge_img.image = cv_image00;
            cv_bridge_img.toImageMsg(ros_msg00);

            if (!options.timestamps)
            {
                cv_bridge_img.header.stamp = ros::Time::now();
                ros_cameraInfoMsg_camera01.header.stamp = cv_bridge_img.header.stamp;
            }
            else
            {

                str_support = dir_timestamp_image02 + "timestamps.txt";
                ifstream timestamps(str_support.c_str());
                if (!timestamps.is_open())
                {
                    ROS_ERROR_STREAM("Fail to open " << timestamps);
                    return 1;
                }
                timestamps.seekg(30*entries_played);
                getline(timestamps,str_support);
                cv_bridge_img.header.stamp = parseTime(str_support).stamp;
                ros_cameraInfoMsg_camera01.header.stamp = cv_bridge_img.header.stamp;
            }
            cv_bridge_img.image = cv_image01;
            cv_bridge_img.toImageMsg(ros_msg01);

            pub00.publish(ros_msg00,ros_cameraInfoMsg_camera00);
            pub01.publish(ros_msg01,ros_cameraInfoMsg_camera01);

        }

        if(options.velodyne || options.all_data)
        {            
            header_support.stamp=ros::Time::now();
            full_filename_velodyne = dir_velodyne_points + boost::str(boost::format("%010d") % entries_played ) + ".bin";

            if (!options.timestamps)
                publish_velodyne(map_pub, full_filename_velodyne,&header_support,options.sendTransform);
            else
            {
                str_support = dir_timestamp_velodyne + "timestamps.txt";
                ifstream timestamps(str_support.c_str());
                if (!timestamps.is_open())
                {
                    ROS_ERROR_STREAM("Fail to open " << timestamps);
                    return 1;
                }
                timestamps.seekg(30*entries_played);
                getline(timestamps,str_support);
                header_support.stamp = parseTime(str_support).stamp;
                publish_velodyne(map_pub, full_filename_velodyne,&header_support,options.sendTransform);
            }


        }

        if(options.gps || options.all_data)
        {
            header_support.stamp=ros::Time::now();
            if (options.timestamps)
            {
                str_support = dir_timestamp_oxts + "timestamps.txt";
                ifstream timestamps(str_support.c_str());
                if (!timestamps.is_open())
                {
                    ROS_ERROR_STREAM("Fail to open " << timestamps);
                    node.shutdown();
                    return 1;
                }
                timestamps.seekg(30*entries_played);
                getline(timestamps,str_support);
                header_support.stamp = parseTime(str_support).stamp;
            }

            full_filename_oxts = dir_oxts + boost::str(boost::format("%010d") % entries_played ) + ".txt";
            if (!getGPS(full_filename_oxts,&ros_msgGpsFix,&header_support))
            {
                ROS_ERROR_STREAM("Fail to open " << full_filename_oxts);
                node.shutdown();
                return 1;
            }



            gps_pub.publish(ros_msgGpsFix);
        }

        if(options.imu || options.all_data)
        {
            header_support.stamp=ros::Time::now();
            if (options.timestamps)
            {
                str_support = dir_timestamp_oxts + "timestamps.txt";
                ifstream timestamps(str_support.c_str());
                if (!timestamps.is_open())
                {
                    ROS_ERROR_STREAM("Fail to open " << timestamps);
                    node.shutdown();
                    return 1;
                }
                timestamps.seekg(30*entries_played);
                getline(timestamps,str_support);
                header_support.stamp = parseTime(str_support).stamp;
            }


            full_filename_oxts = dir_oxts + boost::str(boost::format("%010d") % entries_played ) + ".txt";
            if (!getIMU(full_filename_oxts,&ros_msgImu,&header_support))
            {
                ROS_ERROR_STREAM("Fail to open " << full_filename_oxts);
                node.shutdown();
                return 1;
            }
            imu_pub.publish(ros_msgImu);

            if (options.sendTransform)
            {
                transform.setIdentity();
                transform.setRotation(tf::Quaternion(ros_msgImu.orientation.x,ros_msgImu.orientation.y,ros_msgImu.orientation.z,ros_msgImu.orientation.w));
                br.sendTransform(tf::StampedTransform(transform, ros::Time::now(), ros::this_node::getName(), "imu"));
            }
        }

        ++progress;
        entries_played++;
        loop_rate.sleep();
    }while(entries_played<=total_entries-1 && ros::ok());


    if(options.viewer)
    {
        ROS_INFO_STREAM(" Closing CV viewer(s)");
        if(options.color || options.all_data)
            cv::destroyWindow("CameraSimulator Color Viewer");
        if(options.grayscale || options.all_data)
            cv::destroyWindow("CameraSimulator Grayscale Viewer");
        ROS_INFO_STREAM(" Closing CV viewer(s)... OK");
    }


    ROS_INFO_STREAM("Done!");
    node.shutdown();

    return 0;
}
