#ifndef QTHELPERFROMC_H
#define QTHELPERFROMC_H

bool getProxyString(char **buffer);
bool canReachCloudServer();
void updateWindowTitle();
bool isCloudUrl(const char *filename);
void subsurface_mkdir(const char *dir);
char *get_file_name(const char *fileName);
void copy_image_and_overwrite(const char *cfileName, const char *path, const char *cnewName);
char *hashstring(char *filename);
bool picture_exists(struct picture *picture);
char *move_away(const char *path);
const char *local_file_path(struct picture *picture);
void savePictureLocal(struct picture *picture, const char *data, int len);
void cache_picture(struct picture *picture);
char *cloud_url();
char *hashfile_name_string();
char *picturedir_string();
const char *subsurface_user_agent();
enum deco_mode decoMode();

#endif // QTHELPERFROMC_H
