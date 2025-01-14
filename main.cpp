#include "DataLoader.h"
#include "Dataset.h"
#include "Observation.h"
#include "ConBear.h"
#include <iostream>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <Eigen/Dense>
#include "Visualization.h" // Include Visualization class header

using namespace Eigen;

// Define a struct for pose mean and covariance
class RobotEstimation
{
public:
    std::vector<Pose> poseMeans;
    // std::vector<Matrix3d> poseCovs;
};

double degrees_to_radians(double degrees)
{
    return degrees * M_PI / 180.0; // Convert degrees to radians using the formula
}

int main()
{
    double deltaT = 0.02;                                           // Sample step
    std::vector<double> alphas = {0.2, 0.03, 0.09, 0.08, 0.0, 0.0}; // Motion model noise parameters

    // Measurement model noise parameters
    double sigma_range = 0.43;
    double sigma_bearing = 0.6;
    double sigma_id = 1.0;

    // Q_t matrix
    Matrix3d Q_t;
    Q_t << sigma_range * sigma_range, 0.0, 0.0,
        0.0, sigma_bearing * sigma_bearing, 0.0,
        0.0, 0.0, sigma_id * sigma_id;

    double measurement_prob = 0.0;
    int robot_num = 1;
    int n_landmarks = 15;
    int n_robots = 1;          // Set the number of robots
    double sample_time = 0.02; // Set the sampling time
    std::vector<Barcode> Barcodes;
    std::vector<Landmark_Groundtruth> Landmarks;
    std::vector<Robot> Robots;

    DataLoader dataLoader(n_robots);
    Dataset sampling_dataset;
    // Call the data loading method to load the data
    dataLoader.loadData(Barcodes, Landmarks, Robots);
    dataLoader.printData(Barcodes, Landmarks, Robots);
    auto result = sampling_dataset.sample(Robots, sample_time);
    // sampling_dataset.printSampledData(result);
    // Localization task
    robot_num = robot_num - 1; // Specify the robot number
    std::cout << "Localization task for robot " << Robots[robot_num].sampled_v[0] << std::endl;
    RobotEstimation robotEstimation;

    // Initialize Estimation
    int start = 600;                                  // Start index
    double t = Robots[robot_num].sampled_time[start]; // Start time
    Pose poseMean = {Robots[robot_num].sampled_x[start],
                     Robots[robot_num].sampled_y[start],
                     Robots[robot_num].sampled_theta[start]};

    Matrix3d poseCov;
    poseCov << 0.01, 0.01, 0.01,
        0.01, 0.01, 0.01,
        0.01, 0.01, 0.01;

    // Track which measurement is next received
    int measurementIndex = 0;

    // Set up map between barcodes and landmark IDs (known correlation)
    std::unordered_map<double, double> codeDict;
    for (const Barcode &barcode : Barcodes)
    {
        codeDict[barcode.barcode_num] = barcode.subject_num;
    }

    // Check if time of measurement index is smaller than t = 600
    // while (measurementIndex < Robots[robot_num].time.size() && Robots[robot_num].time[measurementIndex] < t - 0.05)
    while (Robots[robot_num].measurement_time[measurementIndex] < t - 0.05)
    {
        // Update index
        ++measurementIndex;
    }

    // Loop through all odometry and measurement samples
    // Updating the robot's pose estimate with each step
    // Reference table 7.2 in Probabilistic Robotics
    // Starts from 600 for some reason (the first 599 have a problem?)
    for (int i = start; i < Robots[robot_num].sampled_time.size(); ++i)
    {
        double theta = poseMean.theta; // Theta
        // Update time
        t = Robots[robot_num].sampled_time[i];
        // Update movement vector per equation 1
        std::vector<double> u_t = {Robots[robot_num].sampled_v[i], Robots[robot_num].sampled_w[i]}; // [v_t ; omega_t]
        double rot = deltaT * u_t[1];                                                               // rot = delta_theta = deltaT * omega_t
        double halfRot = rot / 2.0;
        double trans = u_t[0] * deltaT; // Translational: v_t * deltaT

        Matrix3d G_t;
        Matrix2d M_t;
        MatrixXd V_t(3, 2); // Define V_t as 3x2 matrix
        Vector3d poseUpdate;

        // Calculate the movement Jacobian per equation 2
        G_t << 1, 0, trans * -sin(theta + halfRot),
            0, 1, trans * cos(theta + halfRot),
            0, 0, 1;

        // Calculate motion covariance in control space per equation 3
        M_t << std::pow(alphas[0] * std::abs(u_t[0]) + alphas[1] * std::abs(u_t[1]), 2), 0,
            0, std::pow(alphas[2] * std::abs(u_t[0]) + alphas[3] * std::abs(u_t[1]), 2);

        // Calculate Jacobian to transform motion covariance to state space per equation 4
        V_t << cos(theta + halfRot), -0.5 * sin(theta + halfRot),
            sin(theta + halfRot), 0.5 * cos(theta + halfRot),
            0, 1;

        // Calculate pose update
        poseUpdate << trans * cos(theta + halfRot),
            trans * sin(theta + halfRot),
            rot; // Assuming u_t(1) is the rotation component (rot)

        // Calculate estimated pose mean per equation 1
        Vector3d poseMeanBar = {poseMean.x + poseUpdate[0],
                                poseMean.y + poseUpdate[1],
                                poseMean.theta + poseUpdate[2]};

        // Calculate estimated pose covariance per equation 5 (Σ^t)
        Matrix3d poseCovBar = G_t * poseCov * G_t.transpose() + V_t * M_t * V_t.transpose();

        Observation get_observations;

        Eigen::MatrixXd z(3, 1); // Initialize z as a 3x1 vector
        // Get measurements for the current timestep, if any
        std::tie(z, measurementIndex) = get_observations.get(Robots, robot_num, t, measurementIndex, codeDict);

        // Initialize zHat and S before processing measurements
        Eigen::MatrixXd zHat;
        std::vector<Matrix3d> S_vector;  // Vector to store S matrices

        // If any measurements are available
        if (z.cols() > 0 && z(2, 0) > 1)
        {
            // Resize zHat based on number of measurements
            zHat.resize(3, z.cols());
            zHat.setZero();  // Set all elements to zero
            // Loop over every measurement
            for (int k = 0; k < z.cols(); ++k)
            {
                int j = static_cast<int>(z(2, k)); // Ensure the index is an integer

                // Get coordinates of the measured landmark
                Vector2d m(Landmarks[j].x, Landmarks[j].y);

                // Compute the expected measurement per equations 6 and 7
                double xDist = m(0) - poseMeanBar(0); // m_j,x − µ^{hat}_t,x
                double yDist = m(1) - poseMeanBar(1);
                double q = xDist * xDist + yDist * yDist;

                ConBear bearing;
                // Constrain expected bearing to between 0 and 2*pi
                double pred_bear = bearing.conBear(atan2(yDist, xDist) - poseMeanBar(2));

                // z_{hat}_t+1|t
                zHat.col(k) << sqrt(q), pred_bear, j;

                // Calculate Jacobian of the measurement model per equation 8
                MatrixXd H(3, 3);
                H << (-1 * (xDist / sqrt(q))), (-1 * (yDist / sqrt(q))), 0,
                    (yDist / q), (-1 * (xDist / q)), -1,
                    0, 0, 0;

                // Compute S per equation 9 (just the parenthesis of Kalman gain)
                Matrix3d S_k = H * poseCovBar * H.transpose() + Q_t;
                S_vector.push_back(S_k);  // Store each S_k in the vector

                // Normalize the residual
                //Vector3d nu = z.col(k) - zHat.col(k);
                //nu(1) = bearing.conBear(nu(1));
                std::cout << "S_k:\n"
                          << S_k << std::endl;
                std::cout << "H:\n"
                          << H << std::endl;          
                std::cout << "zHat:\n"
                          << zHat << std::endl;
                std::cout << "z:\n"
                          << z << std::endl;
                std::cout << "poseMeanBar:\n"
                          << poseMeanBar << std::endl;
                std::cout << "poseCovBar:\n"
                          << poseCovBar << std::endl;
                // Compute Kalman gain per equation 10
                MatrixXd K = poseCovBar * H.transpose() * S_vector[k].inverse();

                // Update pose mean and covariance estimates per equations 11 and 12 (z_t+1 = z_t|t-1)
                poseMeanBar += K * (z.col(k) - zHat.col(k));
                poseCovBar = (Matrix3d::Identity() - (K * H)) * poseCovBar;
            }
        }

        // Store estimation
        robotEstimation.poseMeans.push_back({poseMeanBar[0], poseMeanBar[1], poseMeanBar[2]});
        // Update poseMean with the values of poseMeanBar
        poseMean.x = poseMeanBar[0];
        poseMean.y = poseMeanBar[1];
        poseMean.theta = poseMeanBar[2];
        poseCov = poseCovBar;

        // Calculate error between mean pose and ground truth for testing only
        Vector3d groundtruth;
        groundtruth << Robots[robot_num].x[i],
            Robots[robot_num].y[i],
            Robots[robot_num].theta[i];

        Vector3d error = groundtruth - Vector3d(poseMean.x, poseMean.y, poseMean.theta);
        std::cout << "Error between ground truth and poseMean: " << error.transpose() << std::endl;
    }


    std::cout << "Now we go to the latest loop for visualizing" << std::endl;
    // Initialize SFML visualization
    Visualization vis;
    std::cout << "Now we go to the latest loop for visualizing" << std::endl;

    // Create vectors to store the ground truth values for each timestep
    std::vector<double> groundTruthX;
    std::vector<double> groundTruthY;
    std::vector<double> groundTruthTheta;

    // Populate the ground truth vectors with the robot's sampled values
    for (int i = start; i < Robots[0].sampled_x.size(); ++i)
    {
        groundTruthX.push_back(Robots[0].sampled_x[i]);
        groundTruthY.push_back(Robots[0].sampled_y[i]);
        groundTruthTheta.push_back(Robots[0].sampled_theta[i]);
    }

    std::cout << "Now we go to the latest loop for visualizing" << std::endl;

    // Main loop to simulate continuous updating of the visualization
    for (int i = 0; i < robotEstimation.poseMeans.size(); ++i)
    {
        // Retrieve poseMean for current timestep
        std::vector<Pose> poseMeans = robotEstimation.poseMeans;

        // Draw pose and groundtruth in the visualization
        vis.drawPoseAndGroundtruth(poseMeans, groundTruthX, groundTruthY, groundTruthTheta, i);

        // Display the updated visualization
        vis.display();

        // Handle events to keep the window open until manually closed
        vis.handleEvents();

        // Optional delay to control frame rate
        // sf::sleep(sf::milliseconds(50));  // Adjust as needed
    }

    // Keep the window open until it is manually closed
    while (vis.isOpen())
    {
        vis.handleEvents();
    }

    std::cout << "Finished visualization." << std::endl;

    return 0;
}

