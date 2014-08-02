(module avformat *
(import scheme chicken foreign)

(foreign-declare "#include <libavformat/avformat.h>")

(define av-register-all
  (foreign-lambda void "av_register_all"))

)
