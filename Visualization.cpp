#include "Visualization.h"
#include <cmath>
// Constructor: Initialize SFML window with a size and title
Visualization::Visualization() : window_(sf::VideoMode(800, 600), "Robot Localization Visualization") {
    // Set the initial view
    sf::View view(sf::FloatRect(-500.f, -500.f, 1000.f, 1000.f)); // Adjust the view size and position
    window_.setView(view);

    // Set scaling factors (adjust according to your actual units and window size)
    scaleFactorX_ = 50.f; // 1 meter in real world = 50 pixels in SFML (adjust as needed)
    scaleFactorY_ = 50.f; // 1 meter in real world = 50 pixels in SFML (adjust as needed)
}

// Draw robot's pose means and ground truth
void Visualization::drawPoseAndGroundtruth(const std::vector<Pose>& poseMeans,
                                           const std::vector<double>& sampled_x,
                                           const std::vector<double>& sampled_y,
                                           const std::vector<double>& sampled_theta,
                                           int currentStep) {
    // Clear the window
    window_.clear(sf::Color::White);

    // Draw ground truth (sampled positions)
    if (currentStep < sampled_x.size()) {
        sf::CircleShape groundtruthCircle(20.f); // Increase circle size
        groundtruthCircle.setFillColor(sf::Color::Green);
        groundtruthCircle.setPosition(scaleFactorX_ * sampled_x[currentStep], -scaleFactorY_ * sampled_y[currentStep]);
        window_.draw(groundtruthCircle);
    }

    // Draw poseMeans
    if (currentStep < poseMeans.size()) {
        sf::CircleShape poseCircle(20.f); // Increase circle size
        poseCircle.setFillColor(sf::Color::Blue);
        poseCircle.setPosition(scaleFactorX_ * poseMeans[currentStep].x, -scaleFactorY_ * poseMeans[currentStep].y);
        window_.draw(poseCircle);

        // Draw orientation line (similar to MATLAB script)
        float lineLength = 50.f; // Adjust line length as needed
        sf::Vertex line[] = {
            sf::Vertex(sf::Vector2f(scaleFactorX_ * poseMeans[currentStep].x, -scaleFactorY_ * poseMeans[currentStep].y)),
            sf::Vertex(sf::Vector2f(scaleFactorX_ * (poseMeans[currentStep].x + lineLength * std::cos(poseMeans[currentStep].theta)),
                                    -scaleFactorY_ * (poseMeans[currentStep].y + lineLength * std::sin(poseMeans[currentStep].theta))))
        };
        line[0].color = sf::Color::Red;
        line[1].color = sf::Color::Red;
        window_.draw(line, 2, sf::Lines);
    }

    // Draw landmarks (assuming they are provided similarly to poseMeans)
    // Adjust according to your data structure
    std::vector<Pose> landmarks;  // Replace with actual landmark data if available
    for (const auto& landmark : landmarks) {
        sf::CircleShape landmarkCircle(10.f); // Adjust circle size for landmarks
        landmarkCircle.setFillColor(sf::Color(150, 150, 150)); // Gray color for landmarks
        landmarkCircle.setPosition(scaleFactorX_ * landmark.x, -scaleFactorY_ * landmark.y);
        window_.draw(landmarkCircle);
    }
}

// Display the window
void Visualization::display() {
    window_.display();
}

// Check if the window is open
bool Visualization::isOpen() const {
    return window_.isOpen();
}

// Handle SFML events
void Visualization::handleEvents() {
    sf::Event event;
    while (window_.pollEvent(event)) {
        if (event.type == sf::Event::Closed) {
            window_.close();
        }
    }
}
