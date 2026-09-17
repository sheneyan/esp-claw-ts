#include "provision_button_policy.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(provision_button_classify_press(0) == PROVISION_BUTTON_ACTION_NONE);
    assert(provision_button_classify_press(2999) == PROVISION_BUTTON_ACTION_NONE);
    assert(provision_button_classify_press(3000) == PROVISION_BUTTON_ACTION_REOPEN_AP);
    assert(provision_button_classify_press(9999) == PROVISION_BUTTON_ACTION_REOPEN_AP);
    assert(provision_button_classify_press(10000) == PROVISION_BUTTON_ACTION_FACTORY_RESET);
    assert(provision_button_classify_press(UINT32_MAX) == PROVISION_BUTTON_ACTION_FACTORY_RESET);

    puts("provision_button_policy: all tests passed");
    return 0;
}
