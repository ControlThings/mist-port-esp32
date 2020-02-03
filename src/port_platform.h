/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   port_platform.h
 * Author: jan
 *
 * Created on January 6, 2017, 1:00 PM
 */

#ifndef PORT_PLATFORM_H
#define PORT_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif
    
    
    /**
     * Setup the dependencies for Wish core and Mist.
     */
    void port_platform_deps(void);
    
    /**
     * Initialize the user id list in the core from the contents of the on-disk database.
     * The function also ensures that there is a local identity available.
     * If there are no identities available, the function creates one.
     * @param core
     * @param default_alias The alias of the initial user.
     */
    void port_platform_load_ensure_identities(wish_core_t* core, char* default_alias);
    wish_uid_list_elem_t* port_platform_get_uid_list(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_PLATFORM_H */

