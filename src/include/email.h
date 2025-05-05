/**
 * @brief Set recipient email address for notifications 
 * 
 * @param recipient_email Recipient email address
 * @param config_path Optional path to configuration file
 * @return bool True if successful, false otherwise
 */
bool email_set_recipient(const char *recipient_email, const char *config_path); 

/**
 * @brief Update email configuration
 * 
 * @param email_address Email address used for sending
 * @param email_password Email password
 * @param smtp_server SMTP server hostname 
 * @param smtp_port SMTP server port
 * @param recipient_email Recipient email address (NULL to use sender's address)
 * @param config_path Optional path to configuration file
 * @return bool True if successful, false otherwise
 */
bool email_update_config(const char *email_address, const char *email_password, 
                         const char *smtp_server, int smtp_port,
                         const char *recipient_email, const char *config_path); 