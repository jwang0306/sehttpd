for i in {1..100}; do
    ./htstress -n 10000 -c $i -t 1 -f ./benchmark/$1 http://localhost:8081/
done