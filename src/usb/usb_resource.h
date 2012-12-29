#ifndef freevisa_usb_resource_h_
#define freevisa_usb_resource_h_ 1

#include "resource.h"

#include <openusb.h>

#include "usb_string.h"

namespace freevisa {
namespace usb {

class usb_resource :
        public resource
{
private:
        usb_resource(unsigned int, unsigned int, usb_string const &);
        ~usb_resource() throw();

        virtual ViStatus Close();
        virtual ViStatus ReadSTB(ViUInt16 *);

        openusb_handle_t openusb;
        openusb_dev_handle_t dev;
        uint8_t interface;
        uint8_t int_in_ep;
        uint8_t status_tag;
        unsigned int io_timeout;
        bool have_interrupt_endpoint;

        class creator;
};

}
}

#endif