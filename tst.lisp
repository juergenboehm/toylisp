
(defun 1- (n) (- n 1))

(defun fib (n)
  (labels ((aux (k a b result)
             (cond
               ((= k (- n))
                result)
               ((plusp k)
                (aux (1- k) b (+ a b) result))
               (t
                (let ((prev (- b a)))
                  (aux (1- k) prev a
                       (cons prev result)))))))
    (aux n 0 1 nil)))
    
 (defun fiba (n)
  (labels ((fib-helper (a b count acc)
             (if (zerop count)
                 acc
                 (fib-helper b (+ a b) (1- count) (cons a acc)))))
    (fib-helper 0 1 n '())))

