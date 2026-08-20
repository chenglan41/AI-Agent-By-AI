// http_client.cpp - CURL HTTP client implementation
#include "http_client.h"
#include <curl/curl.h>
#include <sstream>
#include <iostream>

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* str = (std::string*)userp;
    str->append((char*)contents, totalSize);
    return totalSize;
}

HttpClient::HttpClient() : curl_(NULL), lastStatusCode_(0), debug_(false) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_ = curl_easy_init();
}

HttpClient::~HttpClient() {
    if (curl_) {
        curl_easy_cleanup(curl_);
    }
    curl_global_cleanup();
}

void HttpClient::setHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

void HttpClient::setDebug(bool enabled) {
    debug_ = enabled;
}

std::string HttpClient::post(const std::string& url, const std::string& jsonBody) {
    if (!curl_) {
        lastError_ = "CURL not initialized";
        return "";
    }
    
    curl_easy_reset(curl_);
    
    std::string response;
    struct curl_slist* headerList = NULL;
    
    // Debug: Print request info
    if (debug_) {
        std::cout << "[DEBUG] POST Request to: " << url << std::endl;
        std::cout << "[DEBUG] Request body length: " << jsonBody.size() << " bytes" << std::endl;
    }
    
    // Set URL
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    
    // Set headers
    for (std::map<std::string, std::string>::iterator it = headers_.begin(); 
         it != headers_.end(); ++it) {
        std::string header = it->first + ": " + it->second;
        headerList = curl_slist_append(headerList, header.c_str());
    }
    headerList = curl_slist_append(headerList, "Content-Type: application/json");
    
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headerList);
    
    // Set POST data
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, jsonBody.size());
    
    // Set write callback
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    
    // SSL options
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 0L);
    
    // Timeout
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 120L);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl_);
    
    // Get HTTP status code
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &lastStatusCode_);
    
    if (headerList) {
        curl_slist_free_all(headerList);
    }
    
    if (res != CURLE_OK) {
        lastError_ = std::string("CURL Error: ") + curl_easy_strerror(res);
        lastResponse_ = "";
        if (debug_) {
            std::cout << "[DEBUG] " << lastError_ << std::endl;
        }
        return "";
    }
    
    // Save response for debugging
    lastResponse_ = response;
    
    // Debug: Print response info
    if (debug_) {
        std::cout << "[DEBUG] HTTP Status Code: " << lastStatusCode_ << std::endl;
        std::cout << "[DEBUG] Response length: " << response.size() << " bytes" << std::endl;
        
        if (lastStatusCode_ != 200) {
            std::cout << "[DEBUG] Non-200 response: " << response.substr(0, 500) << std::endl;
        }
    }
    
    return response;
}

std::string HttpClient::get(const std::string& url) {
    if (!curl_) {
        lastError_ = "CURL not initialized";
        return "";
    }
    
    curl_easy_reset(curl_);
    
    std::string response;
    struct curl_slist* headerList = NULL;
    
    // Debug: Print request info
    if (debug_) {
        std::cout << "[DEBUG] GET Request to: " << url << std::endl;
    }
    
    // Set URL
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    
    // Set headers
    for (std::map<std::string, std::string>::iterator it = headers_.begin(); 
         it != headers_.end(); ++it) {
        std::string header = it->first + ": " + it->second;
        headerList = curl_slist_append(headerList, header.c_str());
    }
    
    if (headerList) {
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headerList);
    }
    
    // Set write callback
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    
    // SSL options
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 0L);
    
    // Timeout
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 60L);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl_);
    
    // Get HTTP status code
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &lastStatusCode_);
    
    if (headerList) {
        curl_slist_free_all(headerList);
    }
    
    if (res != CURLE_OK) {
        lastError_ = std::string("CURL Error: ") + curl_easy_strerror(res);
        lastResponse_ = "";
        if (debug_) {
            std::cout << "[DEBUG] " << lastError_ << std::endl;
        }
        return "";
    }
    
    // Save response for debugging
    lastResponse_ = response;
    
    // Debug: Print response info
    if (debug_) {
        std::cout << "[DEBUG] HTTP Status Code: " << lastStatusCode_ << std::endl;
        std::cout << "[DEBUG] Response length: " << response.size() << " bytes" << std::endl;
    }
    
    return response;
}

std::string HttpClient::getLastError() const {
    return lastError_;
}

long HttpClient::getLastStatusCode() const {
    return lastStatusCode_;
}

std::string HttpClient::getLastResponse() const {
    return lastResponse_;
}
