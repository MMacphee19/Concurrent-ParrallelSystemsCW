// This is a chopped Pong example from SFML examples

////////////////////////////////////////////////////////////
// Headers
////////////////////////////////////////////////////////////
#include <SFML/Graphics.hpp>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <atomic>
#include <mutex>
#include <thread>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace fs = std::filesystem;

// Helper structure for RGBA pixels (a is safe to ignore for this coursework)
struct rgba_t
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

// Helper function to load RGB data from a file, as a contiguous array (row-major) of RGB triplets, where each of R,G,B is a uint8_t and ranges from 0 to 255
std::vector<rgba_t> load_rgb(const char * filename, int& width, int& height)
{
    int n;
    unsigned char *data = stbi_load(filename, &width, &height, &n, 4);
    const rgba_t* rgbadata = (rgba_t*)(data);
    std::vector<rgba_t> vec;
    vec.assign(rgbadata, rgbadata +width*height);
    stbi_image_free(data);
    return vec;
}

// Conversion to color temperature
double rgbToColorTemperature(rgba_t rgba) {
    // Normalize RGB values to [0, 1]
    double red = rgba.r / 255.0;
    double green = rgba.g / 255.0;
    double blue = rgba.b / 255.0;

    // Apply a gamma correction to RGB values (assumed gamma 2.2)
    red = (red > 0.04045) ? pow((red + 0.055) / 1.055, 2.4) : (red / 12.92);
    green = (green > 0.04045) ? pow((green + 0.055) / 1.055, 2.4) : (green / 12.92);
    blue = (blue > 0.04045) ? pow((blue + 0.055) / 1.055, 2.4) : (blue / 12.92);

    // Convert to XYZ color space
    double X = red * 0.4124 + green * 0.3576 + blue * 0.1805;
    double Y = red * 0.2126 + green * 0.7152 + blue * 0.0722;
    double Z = red * 0.0193 + green * 0.1192 + blue * 0.9505;

    // Calculate chromaticity coordinates
    double x = X / (X + Y + Z);
    double y = Y / (X + Y + Z);

    // Approximate color temperature using McCamy's formula
    double n = (x - 0.3320) / (0.1858 - y);
    double CCT = 449.0 * n*n*n + 3525.0 * n*n + 6823.3 * n + 5520.33;

    return CCT;
}


// Calculate the median from an image filename
double filename_to_median(const std::string& filename, std::vector<float>& timings)
{
    sf::Clock eachImage;
    int width, height;
    auto rgbadata = load_rgb(filename.c_str(), width, height);
    std::vector<double> temperatures;
    std::transform(rgbadata.begin(), rgbadata.end(), std::back_inserter(temperatures), rgbToColorTemperature);
    std::sort(temperatures.begin(), temperatures.end());
    auto median = temperatures.size() % 2 ? 0.5 * (temperatures[temperatures.size() / 2 - 1] + temperatures[temperatures.size() / 2]) : temperatures[temperatures.size() / 2];
    float ImageTimer = eachImage.getElapsedTime().asMilliseconds();
    // printf("%s took %.3f to load \n", filename.c_str(), ImageTimer);
    timings.push_back(ImageTimer);
    return median;
}

// Static sort -- REFERENCE ONLY
void static_sort(std::vector<std::string>& filenames, std::vector<float>& timings)
{
    std::sort(filenames.begin(), filenames.end(), [&timings](const std::string& lhs, const std::string& rhs) {
        return filename_to_median(lhs, timings) < filename_to_median(rhs, timings);
    });
}

sf::Vector2f SpriteScaleFromDimensions(const sf::Vector2u& textureSize, int screenWidth, int screenHeight)
{
    float scaleX = screenWidth / float(textureSize.x);
    float scaleY = screenHeight / float(textureSize.y);
    float scale = std::min(scaleX, scaleY);
    return { scale, scale };
}

std::atomic<bool> sortingComplete(false);

void sortImagesInBackground(
std::vector<std::string>& filenames,
std::vector<std::pair<std::string, double>>& imageData,
std::vector<float>& timings,
std::mutex& dataMutex,
std::atomic<int>& currentIndex
) {
    sf::Clock sortTimer;
    auto workerFunction = [&]() {
        while (true) {
            int myIndex = currentIndex.fetch_add(1);

            if (myIndex >= filenames.size()) {
                break;
            }

            double median = filename_to_median(filenames[myIndex], timings);

            {
                std::lock_guard<std::mutex> lock(dataMutex);
                imageData.push_back({ filenames[myIndex], median });
            }
        }
    };

    std::thread t1(workerFunction);
    std::thread t2(workerFunction);
    std::thread t3(workerFunction);
    // std::thread t4(workerFunction);

    t1.join();
    t2.join();
    t3.join();
    // t4.join();

    std::sort(imageData.begin(), imageData.end(),
        [](const auto& a, const auto& b) {
            return a.second < b.second;
        });

    filenames.clear();
    for (const auto& pair : imageData) {
        filenames.push_back(pair.first);
    }

    float sortTime = sortTimer.getElapsedTime().asMilliseconds();
    printf("Sorting took %.3f milliseconds\n", sortTime);

    std::sort(timings.begin(), timings.end());
    float median;
    if (timings.size() % 2 == 0) {
        median = (timings[timings.size() / 2 - 1] + timings[timings.size() / 2]) / 2.0;
    }
    else {
        median = timings[timings.size() / 2];
    }
    printf("Median: %.3f\n", median);

    sortingComplete = true;
}

int main()
{
    sf::Clock windowTimer;
    std::srand(static_cast<unsigned int>(std::time(NULL)));

    // example folder to load images
    const char* image_folder = "../images/unsorted";
    if (!fs::is_directory(image_folder))
    {
        printf("Directory \"%s\" not found: please make sure it exists, and if it's a relative path, it's under your WORKING directory\n", image_folder);
        return -1;
    }
    std::vector<std::string> imageFilenames;
    for (auto& p : fs::directory_iterator(image_folder))
        imageFilenames.push_back(p.path().u8string());

    ///////////////////////////////////////////////////////////////////////////////////////////////
    //  YOUR CODE HERE INSTEAD, TO ORDER THE IMAGES IN A MULTI-THREADED MANNER WITHOUT BLOCKING  //
    ///////////////////////////////////////////////////////////////////////////////////////////////
    std::atomic<int> currentIndex(0);

    sf::Clock sortTimer;
    std::vector<float> timings;
    std::vector<std::pair<std::string, double>> imageData;
    std::mutex dataMutex;

    // sortImagesInBackground(imageFilenames, imageData, timings, dataMutex, currentIndex);

    std::thread sortingThread(sortImagesInBackground,
        std::ref(imageFilenames),
        std::ref(imageData),
        std::ref(timings),
        std::ref(dataMutex),
        std::ref(currentIndex));
    sortingThread.detach();

    // float sortTime = sortTimer.getElapsedTime().asMilliseconds();
    // printf("Sorting took %.3f milliseconds\n", sortTime);

    // std::sort(timings.begin(), timings.end());
    // float median;
    // if (timings.size() % 2 == 0) {
        // median = (timings[timings.size() / 2 - 1] + timings[timings.size() / 2]) / 2.0;
    // }
    // else {
        // median = timings[timings.size() / 2];
    // }
    // printf("Median: %.3f\n", median);

    // Define some constants
    const int gameWidth = 800;
    const int gameHeight = 600;

    int currentImageIndex = 0;

    // Create the window of the application
    sf::RenderWindow window(sf::VideoMode(gameWidth, gameHeight, 32), "Image Fever",
                            sf::Style::Titlebar | sf::Style::Close);
    window.setVerticalSyncEnabled(true);

    // Load an image to begin with
    // sf::Texture texture;
    // if (!texture.loadFromFile(imageFilenames[currentImageIndex]))
        // return EXIT_FAILURE;
    // sf::Sprite sprite (texture);
    // Make sure the texture fits the screen
    // sprite.setScale(SpriteScaleFromDimensions(texture.getSize(),gameWidth,gameHeight));

    sf::Texture texture;
    sf::Sprite sprite;
    bool firstImageLoaded = false;

    float windowLoadTime = windowTimer.getElapsedTime().asMilliseconds();
    printf("Window opened in %.3f milliseconds\n", windowLoadTime);

    sf::Clock clock;
    while (window.isOpen())
    {
        // Handle events
        sf::Event event;
        while (window.pollEvent(event))
        {
            // Window closed or escape key pressed: exit
            if ((event.type == sf::Event::Closed) ||
               ((event.type == sf::Event::KeyPressed) && (event.key.code == sf::Keyboard::Escape)))
            {
                window.close();
                break;
            }
            
            // Window size changed, adjust view appropriately
            if (event.type == sf::Event::Resized)
            {
                sf::View view;
                view.setSize(gameWidth, gameHeight);
                view.setCenter(gameWidth/2.f, gameHeight/2.f);
                window.setView(view);
            }

            // Arrow key handling!
            if (event.type == sf::Event::KeyPressed)
            {
                if (!sortingComplete) {
                    continue;
                }
                // adjust the image index
                if (event.key.code == sf::Keyboard::Key::Left)
                    currentImageIndex = (currentImageIndex + imageFilenames.size() - 1) % imageFilenames.size();
                else if (event.key.code == sf::Keyboard::Key::Right)
                    currentImageIndex = (currentImageIndex + 1) % imageFilenames.size();
                // get image filename
                const auto& imageFilename = imageFilenames[currentImageIndex];
                // set it as the window title 
                window.setTitle(imageFilename);
                // ... and load the appropriate texture, and put it in the sprite
                if (texture.loadFromFile(imageFilename))
                {
                    sprite = sf::Sprite(texture);
                    sprite.setScale(SpriteScaleFromDimensions(texture.getSize(), gameWidth, gameHeight));
                }
            }
        }

        // Load first image when sorting completes
        if (sortingComplete && !firstImageLoaded) {
            if (texture.loadFromFile(imageFilenames[currentImageIndex])) {
                sprite = sf::Sprite(texture);
                sprite.setScale(SpriteScaleFromDimensions(texture.getSize(), gameWidth, gameHeight));
                firstImageLoaded = true;
                printf("First image loaded after sorting completed\n");
            }
        }

        // Clear the window
        window.clear(sf::Color(0, 0, 0));
        // draw the sprite
        window.draw(sprite);
        // Display things on screen
        window.display();
    }

    return EXIT_SUCCESS;
}
