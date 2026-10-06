#include <rclcpp/rclcpp.hpp>

#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/float64.hpp>

#include <sensor_msgs/msg/nav_sat_fix.hpp>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>


class LoggerUDPReceiver : public rclcpp::Node
{
public:

    LoggerUDPReceiver()
        : Node("logger_udp_receiver")
    {
        /*
         * Parameters
         */

        this->declare_parameter<int>("udp_port", 9000);

        this->declare_parameter<std::string>(
            "csv_file",
            "logger_received.csv");

        udp_port_ =
            this->get_parameter("udp_port")
                .as_int();

        csv_filename_ =
            this->get_parameter("csv_file")
                .as_string();


        /*
         * ROS publishers
         */

        raw_pub_ =
            this->create_publisher<
                std_msgs::msg::String>(
                    "/logger/raw",
                    100);

        date_pub_ =
            this->create_publisher<
                std_msgs::msg::String>(
                    "/logger/date",
                    100);

        time_pub_ =
            this->create_publisher<
                std_msgs::msg::String>(
                    "/logger/time",
                    100);

        field1_pub_ =
            this->create_publisher<
                std_msgs::msg::Float64>(
                    "/logger/field1",
                    100);

        field2_pub_ =
            this->create_publisher<
                std_msgs::msg::Float64>(
                    "/logger/field2",
                    100);

        field3_pub_ =
            this->create_publisher<
                std_msgs::msg::Float64>(
                    "/logger/field3",
                    100);

        field4_pub_ =
            this->create_publisher<
                std_msgs::msg::Float64>(
                    "/logger/field4",
                    100);

        gps_pub_ =
            this->create_publisher<
                sensor_msgs::msg::NavSatFix>(
                    "/gps/fix",
                    100);


        /*
         * Open CSV file
         */

        csv_file_.open(
            csv_filename_,
            std::ios::out |
            std::ios::app);

        if (!csv_file_.is_open())
        {
            RCLCPP_ERROR(
                this->get_logger(),
                "Cannot open CSV file: %s",
                csv_filename_.c_str());

            throw std::runtime_error(
                "Cannot open CSV file");
        }

        /*
         * Write header if file is empty
         */

        csv_file_.seekp(
            0,
            std::ios::end);

        if (csv_file_.tellp() == 0)
        {
            csv_file_
                << "raw_data\n";

            csv_file_.flush();
        }


        /*
         * Create UDP socket
         */

        socket_fd_ =
            socket(
                AF_INET,
                SOCK_DGRAM,
                0);

        if (socket_fd_ < 0)
        {
            RCLCPP_ERROR(
                this->get_logger(),
                "Cannot create UDP socket");

            throw std::runtime_error(
                "UDP socket creation failed");
        }


        /*
         * Configure server address
         */

        sockaddr_in server_address{};

        server_address.sin_family =
            AF_INET;

        server_address.sin_addr.s_addr =
            INADDR_ANY;

        server_address.sin_port =
            htons(udp_port_);


        /*
         * Bind UDP port
         */

        if (bind(
                socket_fd_,
                reinterpret_cast<
                    sockaddr*>(
                        &server_address),
                sizeof(server_address)) < 0)
        {
            RCLCPP_ERROR(
                this->get_logger(),
                "Cannot bind UDP port %d",
                udp_port_);

            close(socket_fd_);

            throw std::runtime_error(
                "UDP bind failed");
        }


        RCLCPP_INFO(
            this->get_logger(),
            "UDP receiver listening on port %d",
            udp_port_);

        RCLCPP_INFO(
            this->get_logger(),
            "CSV file: %s",
            csv_filename_.c_str());


        /*
         * Timer
         *
         * The socket is non-blocking.
         */

        timer_ =
            this->create_wall_timer(
                std::chrono::milliseconds(5),
                std::bind(
                    &LoggerUDPReceiver::receiveData,
                    this));
    }


    ~LoggerUDPReceiver()
    {
        if (csv_file_.is_open())
        {
            csv_file_.close();
        }

        if (socket_fd_ >= 0)
        {
            close(socket_fd_);
        }
    }


private:

    /*
     * Receive UDP packet
     */

    void receiveData()
    {
        char buffer[4096];

        sockaddr_in sender_address{};

        socklen_t sender_length =
            sizeof(sender_address);


        ssize_t received =
            recvfrom(
                socket_fd_,
                buffer,
                sizeof(buffer) - 1,
                MSG_DONTWAIT,
                reinterpret_cast<
                    sockaddr*>(
                        &sender_address),
                &sender_length);


        if (received <= 0)
        {
            return;
        }


        buffer[received] = '\0';


        std::string data(buffer);


        /*
         * Remove trailing CR/LF
         */

        while (!data.empty() &&
               (data.back() == '\r' ||
                data.back() == '\n'))
        {
            data.pop_back();
        }


        if (data.empty())
        {
            return;
        }


        /*
         * Display
         */

        RCLCPP_INFO(
            this->get_logger(),
            "Received: %s",
            data.c_str());


        /*
         * Save exact raw record
         */

        csv_file_
            << data
            << "\n";

        csv_file_.flush();


        /*
         * Publish raw ROS message
         */

        std_msgs::msg::String raw_msg;

        raw_msg.data = data;

        raw_pub_->publish(raw_msg);


        /*
         * Parse record
         */

        parseRecord(data);
    }


    /*
     * Split a string using ';'
     */

    std::vector<std::string>
    splitSemicolon(
        const std::string& text)
    {
        std::vector<std::string> result;

        std::stringstream ss(text);

        std::string item;

        while (std::getline(
            ss,
            item,
            ';'))
        {
            result.push_back(item);
        }

        return result;
    }


    /*
     * Safe conversion to double
     */

    bool toDouble(
        const std::string& text,
        double& value)
    {
        try
        {
            size_t position = 0;

            value =
                std::stod(
                    text,
                    &position);

            return position ==
                   text.size();
        }
        catch (...)
        {
            return false;
        }
    }


    /*
     * Parse the logger record
     */

    void parseRecord(
        const std::string& data)
    {
        auto fields =
            splitSemicolon(data);


        /*
         * Expected beginning:
         *
         * 180826
         * 060438.00
         * 5022.9790
         * 305.5380
         * 28.70
         * $PVHY2,...
         */

        if (fields.size() < 5)
        {
            RCLCPP_WARN(
                this->get_logger(),
                "Record contains fewer than 5 fields");

            return;
        }


        /*
         * Date
         */

        std_msgs::msg::String date_msg;

        date_msg.data =
            fields[0];

        date_pub_->publish(
            date_msg);


        /*
         * Time
         */

        std_msgs::msg::String time_msg;

        time_msg.data =
            fields[1];

        time_pub_->publish(
            time_msg);


        /*
         * Fields 3-5
         *
         * We publish them initially as
         * generic numeric fields.
         *
         * Their scientific meaning should
         * be assigned only after confirming
         * the logger documentation.
         */

        double value;


        if (toDouble(
                fields[2],
                value))
        {
            std_msgs::msg::Float64 msg;

            msg.data = value;

            field1_pub_->publish(msg);
        }


        if (toDouble(
                fields[3],
                value))
        {
            std_msgs::msg::Float64 msg;

            msg.data = value;

            field2_pub_->publish(msg);
        }


        if (toDouble(
                fields[4],
                value))
        {
            std_msgs::msg::Float64 msg;

            msg.data = value;

            field3_pub_->publish(msg);
        }


        /*
         * The fifth numeric field in the
         * original record is currently kept
         * as a generic field.
         */

        if (fields.size() > 5)
        {
            std::string sensor_data =
                fields[5];

            /*
             * Keep this available for future
             * scientific parsing.
             */
        }


        /*
         * GPS
         *
         * Based on the supplied example:
         *
         * 5022.9790
         * 305.5380
         *
         * These appear to use DDMM.MMMM
         * / DDDMM.MMMM style coordinates.
         *
         * We therefore convert them to
         * decimal degrees.
         *
         * This should be confirmed against
         * the logger documentation.
         */

        double latitude_raw;
        double longitude_raw;


        if (toDouble(
                fields[2],
                latitude_raw) &&
            toDouble(
                fields[3],
                longitude_raw))
        {
            double latitude =
                ddmmToDecimal(
                    latitude_raw);

            double longitude =
                ddmmToDecimal(
                    longitude_raw);


            sensor_msgs::msg::NavSatFix gps_msg;

            gps_msg.header.stamp =
                this->get_clock()
                    ->now();

            gps_msg.header.frame_id =
                "gps";

            gps_msg.latitude =
                latitude;

            gps_msg.longitude =
                longitude;

            gps_msg.altitude =
                0.0;

            gps_msg.position_covariance_type =
                sensor_msgs::msg::
                NavSatFix::
                COVARIANCE_TYPE_UNKNOWN;

            gps_pub_->publish(
                gps_msg);
        }
    }


    /*
     * Convert:
     *
     * DDMM.MMMM
     *
     * to:
     *
     * decimal degrees
     */

    double ddmmToDecimal(
        double value)
    {
        double degrees =
            std::floor(
                value / 100.0);

        double minutes =
            value -
            degrees * 100.0;

        return
            degrees +
            minutes / 60.0;
    }


    int socket_fd_{-1};

    int udp_port_{9000};

    std::string csv_filename_;

    std::ofstream csv_file_;


    rclcpp::TimerBase::SharedPtr timer_;


    /*
     * ROS publishers
     */

    rclcpp::Publisher<
        std_msgs::msg::String>::SharedPtr
        raw_pub_;

    rclcpp::Publisher<
        std_msgs::msg::String>::SharedPtr
        date_pub_;

    rclcpp::Publisher<
        std_msgs::msg::String>::SharedPtr
        time_pub_;

    rclcpp::Publisher<
        std_msgs::msg::Float64>::SharedPtr
        field1_pub_;

    rclcpp::Publisher<
        std_msgs::msg::Float64>::SharedPtr
        field2_pub_;

    rclcpp::Publisher<
        std_msgs::msg::Float64>::SharedPtr
        field3_pub_;

    rclcpp::Publisher<
        std_msgs::msg::Float64>::SharedPtr
        field4_pub_;

    rclcpp::Publisher<
        sensor_msgs::msg::NavSatFix>::SharedPtr
        gps_pub_;
};


int main(
    int argc,
    char * argv[])
{
    rclcpp::init(
        argc,
        argv);

    try
    {
        auto node =
            std::make_shared<
                LoggerUDPReceiver>();

        rclcpp::spin(node);
    }
    catch (
        const std::exception& e)
    {
        std::cerr
            << "ERROR: "
            << e.what()
            << std::endl;
    }

    rclcpp::shutdown();

    return 0;
}
