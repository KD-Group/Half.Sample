#include "base.hpp"

namespace Commander {
    namespace Base {

        // ### 1.1 Output
        // output various values to stdout
        void output(bool value) {
            printf(value ? "True" : "False");
        }

        void output(const std::string& value) {
            std::cout << "\"" << value << "\"";
        }

        void output(int value) {
            printf("%d", value);
        }

        void output(double value) {
            printf("%f", value);
        }

        // ### 1.2 Line
        // print a full line like "index = 100"
        void line(const std::string& name, bool value) {
            std::cout << name << " = ";
            output(value);
            std::cout << std::endl;
        }

        void line(const std::string& name, const std::string& value) {
            std::cout << name << " = ";
            output(value);
            std::cout << std::endl;
        }

        void line(const std::string& name, int value) {
            std::cout << name << " = ";
            output(value);
            std::cout << std::endl;
        }

        void line(const std::string& name, double value) {
            std::cout << name << " = ";
            output(value);
            std::cout << std::endl;
        }

        // ### 1.3 Error & End Mark
        void error(Error::Code error_code) {
            line("error", true);

            std::string message = Error::to_string(error_code);
            variable(message)
        }

        void error(const std::string& message) {
            line("error", true);
            variable(message)
        }

        void end() {
            std::cout << "EOF" << std::endl;
        }

    } // namespace Base
} // namespace Commander
